#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include "test_harness.h"
#include <windows.h>
#include <psapi.h>
#include <dbghelp.h>
#include <mmsystem.h>
#pragma comment(lib,"winmm.lib")
#include <ddraw.h>
#include <d3d.h>
#include <intrin.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <float.h>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "engine_bridge.h"
#include "game_sites.h"
#include "widescreen.h"
#include "cursor_image.h"
#include "hud_atlas_mutation.h"
#include "res_mapping.h"
#include "owned_ui_layers.h"
#include "owned_resource_runtime.h"
#include "renderer.h"
#include "graphics_settings.h"
#include "game_focus.h"
#include "cursor_confinement.h"
#include "retained_static_scene.h"
#include "camp_inventory.h"
#include "stall_watchdog.h"

namespace owned {
#ifdef EID3D11_TESTING
struct ResourceStoreConfig;
#endif
namespace {
HMODULE g_module{};
PVOID g_exception_handler{};
std::mutex g_trace_lock;
std::unordered_map<std::string,std::uint64_t> g_counts;
std::atomic<std::uint64_t> g_next_surface_id{0};
// Presented frames; a GDI DIB unused for more than kDcReuseFrames is freed.
std::uint64_t g_present_serial=0;
constexpr std::uint64_t kDcReuseFrames=2;
// A movie on screen: large primary pictures from one surface whose content
// changed since the previous one, less than 250 ms earlier, while no load
// progress movie is open (static loading pictures and progress frames keep
// the cursor). The owned cursor is not drawn until 250 ms after the last
// such frame; game input is untouched. Game thread (Blt and presentVisual).
struct VideoPictureState{std::uint64_t source_id=0,generation=0,last_ms=0,until_ms=0;};
VideoPictureState g_video_picture;
constexpr std::uint64_t kVideoPictureGapMs=250;
void note_large_primary_picture(const SurfaceImage& image) noexcept{
 const std::uint64_t now=GetTickCount64();auto& state=g_video_picture;
 const bool next_frame=state.source_id==image.id&&state.generation!=image.generation&&now-state.last_ms<kVideoPictureGapMs;
 state.source_id=image.id;state.generation=image.generation;state.last_ms=now;
 if(!next_frame)return;
 owned::ProgressMovie movie;if(owned::bridge_progress_movie(movie))return;
 state.until_ms=now+kVideoPictureGapMs;
}
bool video_picture_on_screen() noexcept{return GetTickCount64()<g_video_picture.until_ms;}

class SceneQueueTimingScope {
public:
 SceneQueueTimingScope() noexcept:token_(bridge_scene_queue_timing_begin()){}
 ~SceneQueueTimingScope(){bridge_scene_queue_timing_end(token_);}
 SceneQueueTimingScope(const SceneQueueTimingScope&)=delete;
 SceneQueueTimingScope& operator=(const SceneQueueTimingScope&)=delete;
private:
 SceneQueueTimingToken token_{};
};

class PostEngineSurfaceTimingScope {
public:
 explicit PostEngineSurfaceTimingScope(PostEngineSurfaceTimingKind kind) noexcept
  :token_(bridge_post_engine_surface_timing_begin(kind)){}
 ~PostEngineSurfaceTimingScope(){finish();}
 PostEngineSurfaceTimingScope(const PostEngineSurfaceTimingScope&)=delete;
 PostEngineSurfaceTimingScope& operator=(const PostEngineSurfaceTimingScope&)=delete;
 void finish(std::uint64_t amount=0) noexcept{
  if(finished_)return;
  bridge_post_engine_surface_timing_end(token_,amount);finished_=true;
 }
private:
 PostEngineSurfaceTimingToken token_{};
 bool finished_=false;
};

bool diagnostic_trace_enabled(){static const bool enabled=[](){char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_TRACE_MP",value,sizeof(value))&&value[0]=='1';}();return enabled;}
bool state_trace_enabled(){static const bool enabled=[](){char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_TRACE_STATE",value,sizeof(value))&&value[0]=='1';}();return enabled;}
bool trace_enabled(){static const bool enabled=[](){char value[8]{};return (GetEnvironmentVariableA("EI_RENDERER_TRACE",value,sizeof(value))&&value[0]=='1')||diagnostic_trace_enabled()||state_trace_enabled();}();return enabled;}

void trace(const char* name,const char* fmt=nullptr,...){
 if(!trace_enabled())return;
 std::lock_guard<std::mutex> guard(g_trace_lock);auto n=++g_counts[name];
 if(n!=1&&(n&(n-1))!=0)return;char path[MAX_PATH]{};GetModuleFileNameA(g_module,path,MAX_PATH);char* slash=std::strrchr(path,'\\');if(slash)strcpy_s(slash+1,MAX_PATH-(size_t)(slash+1-path),"owned_renderer.log");FILE* f{};fopen_s(&f,path,"a");if(!f)return;std::fprintf(f,"%s count=%llu",name,(unsigned long long)n);if(fmt){std::fputc(' ',f);va_list ap;va_start(ap,fmt);std::vfprintf(f,fmt,ap);va_end(ap);}std::fputc('\n',f);std::fclose(f);
}

void trace_iid(const char* name,REFIID r){
 trace(name,"iid=%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
  r.Data1,r.Data2,r.Data3,r.Data4[0],r.Data4[1],r.Data4[2],r.Data4[3],
  r.Data4[4],r.Data4[5],r.Data4[6],r.Data4[7]);
}

void enable_application_dpi_awareness(){
 static std::once_flag once;std::call_once(once,[]{SetProcessDPIAware();});
}


// TEMPORARY on-demand sampler: samples the main thread every ~1 ms (EIP plus
// game and own-module return addresses on its stack). Signalling the event
// Local\EID3D11_SampleDump writes the last 5 s to owned_samples_<n>.log.
// Off unless EI_RENDERER_SAMPLER=1: every sample suspends the game thread.
namespace window_sampler {
struct Sample { std::int64_t qpc; std::uint32_t eip; std::uint32_t callers[16]; };
constexpr unsigned kRing=16384;
Sample g_ring[kRing];
std::atomic<unsigned> g_head{0};
HANDLE g_main=nullptr;
std::uintptr_t g_self=0,g_self_end=0;
unsigned g_dumps=0;
void dump(){
 LARGE_INTEGER f,n;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&n);
 const std::int64_t since=n.QuadPart-5*f.QuadPart;
 char path[MAX_PATH]{};GetModuleFileNameA(g_module,path,MAX_PATH);char* slash=std::strrchr(path,'\\');
 if(slash)sprintf_s(slash+1,MAX_PATH-(size_t)(slash+1-path),"owned_samples_%u.log",++g_dumps);
 FILE* file=nullptr;fopen_s(&file,path,"wb");if(!file)return;
 HMODULE modules[512];DWORD needed=0;
 if(EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&needed))
  for(DWORD i=0;i<needed/sizeof(HMODULE)&&i<512;++i){MODULEINFO info{};char name[MAX_PATH]{};
   GetModuleInformation(GetCurrentProcess(),modules[i],&info,sizeof(info));GetModuleBaseNameA(GetCurrentProcess(),modules[i],name,MAX_PATH);
   std::fprintf(file,"module %08lX %08lX %s\n",(unsigned long)(std::uintptr_t)info.lpBaseOfDll,(unsigned long)info.SizeOfImage,name);}
 const unsigned head=g_head.load(std::memory_order_acquire);
 for(unsigned k=1;k<kRing&&k<=head;++k){const Sample& s=g_ring[(head-k)%kRing];if(s.qpc<since)break;
  std::fprintf(file,"s %08lX",(unsigned long)s.eip);for(auto c:s.callers){if(!c)break;std::fprintf(file," %08lX",(unsigned long)c);}std::fprintf(file,"\n");}
 std::fclose(file);
}
DWORD WINAPI run(void*){
 HANDLE request=CreateEventA(nullptr,FALSE,FALSE,"Local\\EID3D11_SampleDump");
 for(;;){
  if(request&&WaitForSingleObject(request,1)==WAIT_OBJECT_0){dump();continue;}
  CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
  if(SuspendThread(g_main)==(DWORD)-1)continue;
  Sample& s=g_ring[g_head.load(std::memory_order_relaxed)%kRing];s={};
  LARGE_INTEGER q;QueryPerformanceCounter(&q);s.qpc=q.QuadPart;
  if(GetThreadContext(g_main,&context)){
   s.eip=context.Eip;unsigned found=0;const auto* slot=reinterpret_cast<const std::uintptr_t*>(context.Esp);
   for(unsigned i=0;i<4096&&found<16;++i){std::uintptr_t value=0;__try{value=slot[i];}__except(EXCEPTION_EXECUTE_HANDLER){break;}
    if(value>=0x401000&&value<0x73a670)s.callers[found++]=static_cast<std::uint32_t>(value);
    else if(value>g_self+0x1000&&value<g_self_end)s.callers[found++]=0x80000000u|static_cast<std::uint32_t>(value-g_self);}
  }
  ResumeThread(g_main);g_head.fetch_add(1,std::memory_order_release);
 }
#pragma warning(suppress:4702)
 return 0;
}
// Frames over 40 ms (present to present; EI_RENDERER_HITCH_MS sets another
// threshold) dump their samples automatically to owned_hitches.log (at most
// 300 dumps).
std::int64_t g_previous_present=0;unsigned g_hitch_dumps=0;
double hitch_threshold_ms() noexcept{
 static const double threshold=[]{
  char value[16]{};
  if(!GetEnvironmentVariableA("EI_RENDERER_HITCH_MS",value,sizeof(value)))return 40.0;
  const double parsed=std::atof(value);
  return parsed>=1.0&&parsed<=10000.0?parsed:40.0;
 }();
 return threshold;
}
void present(){
 if(!g_main)return;
 LARGE_INTEGER f,n;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&n);
 const std::int64_t previous=g_previous_present;g_previous_present=n.QuadPart;
 const double ms=previous?double(n.QuadPart-previous)*1000.0/double(f.QuadPart):0;
 if(ms<hitch_threshold_ms()||g_hitch_dumps>=300)return;
 char path[MAX_PATH]{};GetModuleFileNameA(g_module,path,MAX_PATH);char* slash=std::strrchr(path,'\\');
 if(slash)strcpy_s(slash+1,MAX_PATH-(size_t)(slash+1-path),"owned_hitches.log");
 // One session per file: the first dump keeps the previous session as
 // owned_hitches.prev.log and starts a fresh log.
 const bool first=g_hitch_dumps==0;
 if(first&&slash){char previous_path[MAX_PATH]{};strcpy_s(previous_path,path);
  strcpy_s(previous_path+(slash+1-path),MAX_PATH-(size_t)(slash+1-path),"owned_hitches.prev.log");
  MoveFileExA(path,previous_path,MOVEFILE_REPLACE_EXISTING);}
 FILE* file=nullptr;fopen_s(&file,path,first?"wb":"ab");if(!file)return;
 if(!g_hitch_dumps++){
  HMODULE modules[512];DWORD needed=0;
  if(EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&needed))
   for(DWORD i=0;i<needed/sizeof(HMODULE)&&i<512;++i){MODULEINFO info{};char name[MAX_PATH]{};
    GetModuleInformation(GetCurrentProcess(),modules[i],&info,sizeof(info));GetModuleBaseNameA(GetCurrentProcess(),modules[i],name,MAX_PATH);
    std::fprintf(file,"module %08lX %08lX %s\n",(unsigned long)(std::uintptr_t)info.lpBaseOfDll,(unsigned long)info.SizeOfImage,name);}
 }
 std::fprintf(file,"hitch %.2f tick=%lu\n",ms,(unsigned long)GetTickCount());
 const unsigned head=g_head.load(std::memory_order_acquire);
 for(unsigned k=1;k<kRing&&k<=head;++k){const Sample& sample=g_ring[(head-k)%kRing];if(sample.qpc<previous)break;if(sample.qpc>n.QuadPart)continue;
  std::fprintf(file,"s %08lX",(unsigned long)sample.eip);for(auto c:sample.callers){if(!c)break;std::fprintf(file," %08lX",(unsigned long)c);}std::fprintf(file,"\n");}
 std::fclose(file);
}
void start(){
 if(g_main)return;
 // The process keeps the 1 ms timer resolution it had with the sampler running.
 static bool timer_set=false;if(!timer_set){timer_set=true;timeBeginPeriod(1);}
 {char value[8]{};if(!GetEnvironmentVariableA("EI_RENDERER_SAMPLER",value,sizeof(value))||value[0]!='1')return;}
 MODULEINFO info{};GetModuleInformation(GetCurrentProcess(),g_module,&info,sizeof(info));
 g_self=reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);g_self_end=g_self+info.SizeOfImage;
 g_main=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,GetCurrentThreadId());
 if(g_main)CloseHandle(CreateThread(nullptr,0,run,nullptr,0,nullptr));
}
}

// GPU stall watchdog (stall_watchdog.h): once per session, a main-thread
// bracket open for more than 5 s writes a gpu_stall line to bridge.jsonl and
// gpu-stall-<timestamp>.dmp beside it (%LOCALAPPDATA%\EID3D11, or
// EI_RENDERER_DEBUG_DIR). It only records: no recovery, no termination. The
// stalled thread is inside the driver, so the record avoids the CRT and heap
// where it can (Win32 file calls, stack buffers, dbghelp loaded at start).
namespace stall_watchdog {
using WriteDumpFn=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,
 PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
constexpr std::int64_t kStallSeconds=5;
WriteDumpFn g_write_dump=nullptr;
HANDLE g_thread=nullptr,g_stop=nullptr,g_done=nullptr;
char g_root[MAX_PATH]{};
void append(const char* event,const char* detail){
 char path[MAX_PATH+16];wsprintfA(path,"%s\\bridge.jsonl",g_root);
 HANDLE file=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)return;
 char line[640];const int length=wsprintfA(line,"{\"event\":\"%s\",\"detail\":\"%s\",\"sites_verified\":false}\n",event,detail);
 DWORD written=0;WriteFile(file,line,static_cast<DWORD>(length),&written,nullptr);CloseHandle(file);
}
void record(const char* bracket,DWORD thread,std::int64_t tenths){
 SYSTEMTIME t{};GetLocalTime(&t);char name[64];
 wsprintfA(name,"gpu-stall-%04u%02u%02u-%02u%02u%02u.dmp",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
 char detail[320];
 wsprintfA(detail,"bracket=%s seconds=%lu.%lu thread=%lu dump=%s",bracket?bracket:"?",
  static_cast<unsigned long>(tenths/10),static_cast<unsigned long>(tenths%10),thread,name);
 append("gpu_stall",detail);
 if(!g_write_dump){append("gpu_stall_dump","result=dbghelp_unavailable");return;}
 char path[MAX_PATH+80];wsprintfA(path,"%s\\%s",g_root,name);
 HANDLE file=CreateFileA(path,GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE){wsprintfA(detail,"result=create_failed error=%lu",GetLastError());append("gpu_stall_dump",detail);return;}
 // Every thread with its stack and TEB, private read/write memory (the
 // driver's heaps and lock objects), module data sections (driver globals),
 // the handle table and the region map. Image and mapped-file contents
 // are left out: the analysis loads them from the binaries.
 const auto type=static_cast<MINIDUMP_TYPE>(MiniDumpWithPrivateReadWriteMemory|MiniDumpWithDataSegs|
  MiniDumpWithHandleData|MiniDumpWithThreadInfo|MiniDumpWithFullMemoryInfo|MiniDumpWithUnloadedModules);
 MINIDUMP_USER_STREAM comment{CommentStreamA,static_cast<ULONG>(std::strlen(detail)+1),detail};
 MINIDUMP_USER_STREAM_INFORMATION streams{1,&comment};
 const BOOL ok=g_write_dump(GetCurrentProcess(),GetCurrentProcessId(),file,type,nullptr,&streams,nullptr);
 const DWORD error=ok?0:GetLastError();
 LARGE_INTEGER size{};GetFileSizeEx(file,&size);CloseHandle(file);
 wsprintfA(detail,"result=%s error=%08lx mb=%lu dump=%s",ok?"ok":"failed",error,
  static_cast<unsigned long>(size.QuadPart>>20),name);
 append("gpu_stall_dump",detail);
}
DWORD WINAPI run(void*){
 LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
 namespace sw=owned::stall_watch;
 std::int64_t watched[sw::kSlots]{};std::int64_t wakes[sw::kSlots]{};
 // A stall needs the same bracket entry on more than kStallSeconds
 // consecutive 1 s wakes as well as on the clock, so a system sleep or a
 // suspended process, which skip the wakes, never counts as one. Slot 0 is
 // the game thread, slot 1 the render thread (render_thread option).
 bool recorded=false;
 while(!recorded&&WaitForSingleObject(g_stop,1000)==WAIT_TIMEOUT){
  for(int s=0;s<sw::kSlots&&!recorded;++s){
   const std::int64_t since=sw::slots[s].busy_since.load(std::memory_order_acquire);
   if(!since||since!=watched[s]){watched[s]=since;wakes[s]=0;continue;}
   const std::int64_t ticks=sw::now()-since;
   if(++wakes[s]<kStallSeconds||ticks<=kStallSeconds*frequency.QuadPart||IsDebuggerPresent())continue;
   record(sw::slots[s].busy_name.load(std::memory_order_relaxed),
    sw::slots[s].busy_thread.load(std::memory_order_relaxed),ticks*10/frequency.QuadPart);
   recorded=true;
  }
 }
 SetEvent(g_done);
 return 0;
}
void start(){
 if(g_thread)return;
 // The bridge.jsonl folder (engine_bridge.cpp debug_root).
 char value[MAX_PATH]{};DWORD length=0;
 if((length=GetEnvironmentVariableA("EI_RENDERER_DEBUG_DIR",value,MAX_PATH))!=0&&length<MAX_PATH)strcpy_s(g_root,value);
 else if((length=GetEnvironmentVariableA("LOCALAPPDATA",value,MAX_PATH))!=0&&length<MAX_PATH-8)wsprintfA(g_root,"%s\\EID3D11",value);
 else strcpy_s(g_root,".\\EID3D11-debug");
 CreateDirectoryA(g_root,nullptr);
 if(HMODULE dbghelp=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32))
  g_write_dump=reinterpret_cast<WriteDumpFn>(GetProcAddress(dbghelp,"MiniDumpWriteDump"));
 g_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);g_done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 if(g_stop&&g_done)g_thread=CreateThread(nullptr,0,run,nullptr,0,nullptr);
 if(g_thread)SetThreadPriority(g_thread,THREAD_PRIORITY_ABOVE_NORMAL);
}
// DLL unload (FreeLibrary): the loader lock is held, so the thread cannot
// finish exiting here; wait only until it has left this module's code. At
// process exit Windows has already ended the thread.
void stop(){
 if(!g_thread)return;
 SetEvent(g_stop);WaitForSingleObject(g_done,2000);
 CloseHandle(g_thread);g_thread=nullptr;
}
}
// Temporary crash diagnostic for the world-teardown crash at 005D58EA (issue
// #161), opt-in: EID3D11_FEAR_WATCH=1. The CUMFear vtable (0073FF8C, .rdata)
// slots for the deleting destructor (0), GetType (8) and Load (11) point at
// thunks that record the object and the call-validated game return addresses
// on its stack, then jump to the stock function; no code bytes change.
// Deletions go to a fixed ring, first sightings to a table that a 20 ms
// watcher uses to time a vtable change of a still-live object. A first-chance
// access violation at 005D58EA looks up ecx, writes one fear_watch_crash line
// to bridge.jsonl and a full-memory dump, and continues the search. In that
// line deleted=yes means a virtual deletion of this address was recorded
// after its latest sighting (freed while still listed); deleted=no with a
// vtable_change time means the live object was overwritten. Verified
// on game.exe SHA-256 be0fc330...; any differing slot or byte refuses the
// install. Remove once the crash is understood.
namespace fear_watch {
constexpr std::uint32_t kVtable=0x0073FF8C,kDelete=0x005D2930,kDuplicate=0x005D2880,kGetType=0x005D2920,
 kRemoveReferences=0x004BA230,kLoad=0x005D3690,kCrashSite=0x005D58EA;
constexpr std::uint32_t kRing=1u<<16,kSeen=1u<<14;constexpr int kFrames=10,kSeenFrames=6;
struct Deletion{volatile LONG seq;std::uint32_t object,thread,flag;std::int64_t ticks;std::uint32_t frames[kFrames];};
struct Seen{volatile LONG object,deleted;std::uint32_t thread,source;std::int64_t ticks;std::uint32_t frames[kSeenFrames];
 volatile LONG changed_value;std::int64_t changed_ticks;};
Deletion* g_ring=nullptr;Seen* g_seen=nullptr;volatile LONG g_next=0,g_seen_drops=0,g_crash_taken=0;
std::uintptr_t g_image=0,g_text=0,g_text_end=0;std::uint32_t* g_slots=nullptr;
std::uint32_t g_original_delete=0,g_original_get_type=0,g_original_load=0;
PVOID g_handler=nullptr;HANDLE g_watch_stop=nullptr;
std::int64_t now(){LARGE_INTEGER t{};QueryPerformanceCounter(&t);return t.QuadPart;}
// Game-relative addresses (0x400000 base) of game .text return addresses on
// the stack: the game does not keep frame pointers, so a frame walk stops
// early. A value counts when a call instruction ends exactly before it.
int scan_stack(const std::uint32_t* esp,std::uint32_t* out,int capacity){
 const auto stack_base=static_cast<std::uintptr_t>(__readfsdword(4));int found=0;
 for(int i=0;i<384&&found<capacity;++i){
  if(reinterpret_cast<std::uintptr_t>(esp+i+1)>stack_base)break;
  const std::uintptr_t v=esp[i];if(v<g_text+6||v>=g_text_end)continue;
  const auto* c=reinterpret_cast<const std::uint8_t*>(v);
  if(c[-5]==0xE8||(c[-2]==0xFF&&((c[-1]>>3)&7)==2)||(c[-3]==0xFF&&((c[-2]>>3)&7)==2)||(c[-6]==0xFF&&((c[-5]>>3)&7)==2))
   out[found++]=static_cast<std::uint32_t>(v-g_image+0x400000u);
 }
 return found;
}
Seen* find_seen(std::uint32_t object,bool claim){
 const std::uint32_t start=(object>>3)*2654435761u>>(32-14);
 for(std::uint32_t i=0;i<64;++i){Seen& s=g_seen[(start+i)&(kSeen-1)];
  const LONG held=s.object;if(held==static_cast<LONG>(object))return &s;
  if(!held){if(!claim)return nullptr;
   const LONG prior=InterlockedCompareExchange(&s.object,static_cast<LONG>(object),0);
   if(!prior||prior==static_cast<LONG>(object))return &s;}
 }
 if(claim)InterlockedIncrement(&g_seen_drops);
 return nullptr;
}
void __stdcall on_delete(std::uint32_t object,const std::uint32_t* esp){
 const LONG seq=InterlockedIncrement(&g_next);
 Deletion& d=g_ring[static_cast<std::uint32_t>(seq-1)&(kRing-1)];
 d.seq=0;d.object=object;d.thread=GetCurrentThreadId();d.flag=esp[1];d.ticks=now();
 std::memset(d.frames,0,sizeof(d.frames));scan_stack(esp,d.frames,kFrames);
 InterlockedExchange(&d.seq,seq);
 if(Seen* s=find_seen(object,false))InterlockedExchange(&s->deleted,1);
}
// source 1 = GetType (called as a fear is added), 2 = Load. A sighting is
// refreshed after its object was deleted (address reuse) or on every Load.
void __stdcall on_seen(std::uint32_t object,const std::uint32_t* esp,std::uint32_t source){
 Seen* s=find_seen(object,true);if(!s)return;
 if(s->ticks&&!s->deleted&&source==1)return;
 s->deleted=1;s->thread=GetCurrentThreadId();s->source=source;s->changed_ticks=0;s->changed_value=0;
 std::memset(s->frames,0,sizeof(s->frames));scan_stack(esp,s->frames,kSeenFrames);s->ticks=now();
 InterlockedExchange(&s->deleted,0);
}
__declspec(naked) void delete_thunk(){__asm{
 pushad
 lea eax,[esp+32]
 push eax
 push ecx
 call on_delete
 popad
 jmp dword ptr [g_original_delete]
}}
__declspec(naked) void get_type_thunk(){__asm{
 pushad
 lea eax,[esp+32]
 push 1
 push eax
 push ecx
 call on_seen
 popad
 jmp dword ptr [g_original_get_type]
}}
__declspec(naked) void load_thunk(){__asm{
 pushad
 lea eax,[esp+32]
 push 2
 push eax
 push ecx
 call on_seen
 popad
 jmp dword ptr [g_original_load]
}}
// A live (not deleted) object whose vtable dword stops being CUMFear's gets
// the time and the new value, once per sighting. The destructor thunk marks
// the object deleted before the stock destructor rewrites the vtable, so
// the second deleted check rejects a legitimate deletion.
DWORD WINAPI watch(void*){
 while(WaitForSingleObject(g_watch_stop,20)==WAIT_TIMEOUT){
  for(std::uint32_t i=0;i<kSeen;++i){Seen& s=g_seen[i];
   const LONG object=s.object;if(!object||s.deleted||s.changed_ticks||!s.ticks)continue;
   std::uint32_t value=0;
   __try{value=*reinterpret_cast<const volatile std::uint32_t*>(static_cast<std::uintptr_t>(object));}
   __except(EXCEPTION_EXECUTE_HANDLER){value=0xFFFFFFFFu;}
   if(value==g_image+kVtable-0x400000u||s.deleted)continue;
   s.changed_value=static_cast<LONG>(value);s.changed_ticks=now();
  }
 }
 return 0;
}
void format_frames(char* out,const std::uint32_t* frames,int count){
 int used=0;out[0]='-';out[1]=0;
 for(int i=0;i<count&&frames[i];++i)used+=wsprintfA(out+used,i?",%08lX":"%08lX",static_cast<unsigned long>(frames[i]));
}
unsigned long ms_before(std::int64_t then,std::int64_t at){
 LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return then?static_cast<unsigned long>((at-then)*1000/f.QuadPart):0ul;
}
LONG CALLBACK on_exception(EXCEPTION_POINTERS* info){
 if(info->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||
  reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress)!=g_image+kCrashSite-0x400000u||
  InterlockedExchange(&g_crash_taken,1))return EXCEPTION_CONTINUE_SEARCH;
 const std::int64_t at=now();const CONTEXT* c=info->ContextRecord;const std::uint32_t object=c->Ecx;
 const Deletion* last=nullptr;unsigned long deletions=0;
 for(std::uint32_t i=0;i<kRing;++i){const Deletion& d=g_ring[i];
  if(!d.seq||d.object!=object)continue;++deletions;if(!last||d.seq>last->seq)last=&d;}
 std::uint32_t head[2]{};
 __try{head[0]=reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(object))[0];
  head[1]=reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(object))[1];}
 __except(EXCEPTION_EXECUTE_HANDLER){head[0]=head[1]=0xFFFFFFFFu;}
 const Seen* seen=find_seen(object,false);if(seen&&!seen->ticks)seen=nullptr;
 char delete_stack[128],seen_stack[80];
 format_frames(delete_stack,last?last->frames:nullptr,last?kFrames:0);
 format_frames(seen_stack,seen?seen->frames:nullptr,seen?kSeenFrames:0);
 char detail[600];
 wsprintfA(detail,"object=%08lX head=%08lX,%08lX deleted=%s deletions_of_object=%lu delete_ms_before_crash=%lu delete_thread=%lu delete_flag=%lu "
  "delete_stack=%s crash_thread=%lu seen=%s seen_ms_before_crash=%lu seen_thread=%lu seen_stack=%s vtable_change_ms_before_crash=%lu "
  "vtable_change_value=%08lX edi=%08lX esi=%08lX ring_total=%lu seen_drops=%ld",
  static_cast<unsigned long>(object),static_cast<unsigned long>(head[0]),static_cast<unsigned long>(head[1]),!last?"no":!seen||last->ticks>=seen->ticks?"yes":"before_last_sighting",deletions,
  last?ms_before(last->ticks,at):0ul,last?static_cast<unsigned long>(last->thread):0ul,last?static_cast<unsigned long>(last->flag):0ul,
  delete_stack,GetCurrentThreadId(),!seen?"no":seen->source==2?"load":"get_type",seen?ms_before(seen->ticks,at):0ul,
  seen?static_cast<unsigned long>(seen->thread):0ul,seen_stack,seen&&seen->changed_ticks?ms_before(seen->changed_ticks,at):0ul,
  seen?static_cast<unsigned long>(seen->changed_value):0ul,static_cast<unsigned long>(c->Edi),static_cast<unsigned long>(c->Esi),
  static_cast<unsigned long>(g_next),g_seen_drops);
 stall_watchdog::append("fear_watch_crash",detail);
 if(!stall_watchdog::g_write_dump){stall_watchdog::append("fear_watch_dump","result=dbghelp_unavailable");return EXCEPTION_CONTINUE_SEARCH;}
 SYSTEMTIME t{};GetLocalTime(&t);char name[64],path[MAX_PATH+80];
 wsprintfA(name,"crash-005D58EA-%04u%02u%02u-%02u%02u%02u.dmp",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
 wsprintfA(path,"%s\\%s",stall_watchdog::g_root,name);
 HANDLE file=CreateFileA(path,GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE){wsprintfA(detail,"result=create_failed error=%lu",GetLastError());stall_watchdog::append("fear_watch_dump",detail);return EXCEPTION_CONTINUE_SEARCH;}
 MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(),info,FALSE};
 const auto type=static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory|MiniDumpWithHandleData|MiniDumpWithThreadInfo|
  MiniDumpWithFullMemoryInfo|MiniDumpWithUnloadedModules);
 const BOOL ok=stall_watchdog::g_write_dump(GetCurrentProcess(),GetCurrentProcessId(),file,type,&exception,nullptr,nullptr);
 const DWORD error=ok?0:GetLastError();LARGE_INTEGER size{};GetFileSizeEx(file,&size);CloseHandle(file);
 wsprintfA(detail,"result=%s error=%08lx mb=%lu dump=%s",ok?"ok":"failed",error,static_cast<unsigned long>(size.QuadPart>>20),name);
 stall_watchdog::append("fear_watch_dump",detail);
 return EXCEPTION_CONTINUE_SEARCH;
}
bool write_slot(int slot,std::uint32_t value){
 DWORD protection=0,ignored=0;if(!VirtualProtect(g_slots+slot,4,PAGE_READWRITE,&protection))return false;
 InterlockedExchange(reinterpret_cast<volatile LONG*>(g_slots+slot),static_cast<LONG>(value));
 VirtualProtect(g_slots+slot,4,protection,&ignored);return true;
}
void install(){
 static bool attempted=false;if(attempted)return;attempted=true;
 char value[4]{};if(GetEnvironmentVariableA("EID3D11_FEAR_WATCH",value,sizeof(value))!=1||value[0]!='1')return;
 g_image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
 const auto rebase=[](std::uint32_t va){return g_image+va-0x400000u;};
 const auto* headers=reinterpret_cast<const IMAGE_NT_HEADERS*>(g_image+reinterpret_cast<const IMAGE_DOS_HEADER*>(g_image)->e_lfanew);
 const auto* text=IMAGE_FIRST_SECTION(headers);
 g_text=g_image+text->VirtualAddress;g_text_end=g_text+text->Misc.VirtualSize;
 g_slots=reinterpret_cast<std::uint32_t*>(rebase(kVtable));
 // The stock slot values, then the stock bytes of the hooked functions and
 // of the crash site (mov ecx,[eax+8]; mov eax,[ecx]; call [eax+28h]).
 static const std::uint8_t delete_bytes[]={0x56,0x8B,0xF1,0xE8,0x18,0x00,0x00,0x00,0xF6,0x44,0x24,0x08,0x01,0x74,0x09,0x56,
  0xE8,0x93,0xDC,0x12,0x00,0x83,0xC4,0x04,0x8B,0xC6,0x5E,0xC2,0x04,0x00};
 static const std::uint8_t get_type_bytes[]={0xB8,0x07,0x00,0x00,0x00,0xC3};
 static const std::uint8_t load_bytes[]={0x56,0x8B,0x74,0x24,0x08,0x57,0x8B,0xF9};
 static const std::uint8_t crash_bytes[]={0x8B,0x48,0x08,0x8B,0x01,0xFF,0x50,0x28};
 const std::uint32_t expected[][2]={{0,kDelete},{3,kDuplicate},{8,kGetType},{10,kRemoveReferences},{11,kLoad}};
 const char* reason=nullptr;
 if(text->VirtualAddress!=0x1000||std::memcmp(text->Name,".text",6))reason="text_section_differs";
 for(const auto& e:expected)if(!reason&&g_slots[e[0]]!=rebase(e[1]))reason="vtable_slot_differs";
 if(!reason&&(std::memcmp(reinterpret_cast<void*>(rebase(kDelete)),delete_bytes,sizeof(delete_bytes))||
  std::memcmp(reinterpret_cast<void*>(rebase(kGetType)),get_type_bytes,sizeof(get_type_bytes))||
  std::memcmp(reinterpret_cast<void*>(rebase(kLoad)),load_bytes,sizeof(load_bytes))||
  std::memcmp(reinterpret_cast<void*>(rebase(kCrashSite-5)),crash_bytes,sizeof(crash_bytes))))reason="function_bytes_differ";
 if(!reason){
  g_ring=static_cast<Deletion*>(VirtualAlloc(nullptr,sizeof(Deletion)*kRing,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  g_seen=static_cast<Seen*>(VirtualAlloc(nullptr,sizeof(Seen)*kSeen,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  g_watch_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if(!g_ring||!g_seen||!g_watch_stop)reason="allocation_failed";
 }
 if(reason){stall_watchdog::append("fear_watch_rejected",reason);return;}
 g_handler=AddVectoredExceptionHandler(1,on_exception);
 g_original_delete=g_slots[0];g_original_get_type=g_slots[8];g_original_load=g_slots[11];
 const bool ok=write_slot(0,reinterpret_cast<std::uint32_t>(&delete_thunk))&&write_slot(8,reinterpret_cast<std::uint32_t>(&get_type_thunk))&&
  write_slot(11,reinterpret_cast<std::uint32_t>(&load_thunk));
 if(HANDLE thread=CreateThread(nullptr,0,watch,nullptr,0,nullptr))CloseHandle(thread);
 char detail[160];wsprintfA(detail,"result=%s vtable=%08lX ring=%lu seen=%lu",ok?"ok":"protect_failed",
  static_cast<unsigned long>(kVtable),static_cast<unsigned long>(kRing),static_cast<unsigned long>(kSeen));
 stall_watchdog::append("fear_watch_installed",detail);
}
// DLL unload only: put the stock slots back and stop observing.
void uninstall(){
 if(!g_original_delete)return;
 write_slot(0,g_original_delete);write_slot(8,g_original_get_type);write_slot(11,g_original_load);g_original_delete=0;
 if(g_watch_stop)SetEvent(g_watch_stop);
 if(g_handler){RemoveVectoredExceptionHandler(g_handler);g_handler=nullptr;}
}
}
// Startup crash #160: Windows dinput.dll (32-bit, PE timestamp 871200FE)
// walks its HID device table (count, capacity, then 0x48-byte entries) in
// the function at +1A304 and reads entry[i]+44 before it checks i < count
// (+1A353: cmp [esi+44h],0 / je +1A3AD; cmp edi,[ecx] / jge +1A3AD). With a
// full table that read lands one entry past the block; at a heap commit edge
// it faults inside DirectInputCreate (game CInputManager::Create). Once
// edi >= count both branches go to +1A3AD, so resuming there is exactly the
// stock result. Installed at DLL attach, before the game's WinMain.
namespace dinput_guard {
constexpr std::uint32_t kSite=0x1A353,kResume=0x1A3AD,kTimestamp=0x871200FE,kImageSize=0x27000;
volatile LONG g_fired=0,g_logged=0;char g_detail[160]{};
void log_pending(){
 if(g_fired&&stall_watchdog::g_root[0]&&!InterlockedExchange(&g_logged,1))stall_watchdog::append("dinput_overread_guard",g_detail);
}
LONG CALLBACK on_exception(EXCEPTION_POINTERS* info){
 const EXCEPTION_RECORD* record=info->ExceptionRecord;CONTEXT* c=info->ContextRecord;
 if(record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||record->NumberParameters<2||record->ExceptionInformation[0]!=0||!c)
  return EXCEPTION_CONTINUE_SEARCH;
 HMODULE owner=nullptr;
 if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
  static_cast<LPCSTR>(record->ExceptionAddress),&owner)||!owner)return EXCEPTION_CONTINUE_SEARCH;
 const auto base=reinterpret_cast<std::uintptr_t>(owner);
 if(reinterpret_cast<std::uintptr_t>(record->ExceptionAddress)!=base+kSite)return EXCEPTION_CONTINUE_SEARCH;
 static const std::uint8_t site[]={0x85,0xF6,0x74,0x5A,0x83,0x7E,0x44,0x00,0x74,0x54,0x3B,0x39,0x7D,0x50};
 static const std::uint8_t resume[]={0x85,0xC0,0x0F,0x84,0xE4,0x00,0x00,0x00};
 const auto* headers=reinterpret_cast<const IMAGE_NT_HEADERS*>(base+reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
 if(headers->FileHeader.TimeDateStamp!=kTimestamp||headers->OptionalHeader.SizeOfImage!=kImageSize||
  std::memcmp(reinterpret_cast<const void*>(base+kSite-4),site,sizeof(site))||
  std::memcmp(reinterpret_cast<const void*>(base+kResume),resume,sizeof(resume))||
  record->ExceptionInformation[1]!=c->Esi+0x44||!c->Ecx)return EXCEPTION_CONTINUE_SEARCH;
 LONG count=0;
 __try{count=*reinterpret_cast<const volatile LONG*>(static_cast<std::uintptr_t>(c->Ecx));}
 __except(EXCEPTION_EXECUTE_HANDLER){return EXCEPTION_CONTINUE_SEARCH;}
 if(static_cast<LONG>(c->Edi)<count)return EXCEPTION_CONTINUE_SEARCH;
 if(!InterlockedExchange(&g_fired,1))
  wsprintfA(g_detail,"resumed=dinput+%05lX table=%08lX count=%ld index=%lu fault=%08lX thread=%lu",
   static_cast<unsigned long>(kResume),static_cast<unsigned long>(c->Ecx),count,static_cast<unsigned long>(c->Edi),
   static_cast<unsigned long>(record->ExceptionInformation[1]),GetCurrentThreadId());
 c->Eip=static_cast<DWORD>(base+kResume);
 log_pending();
 return EXCEPTION_CONTINUE_EXECUTION;
}
PVOID g_handler=nullptr;
}
// The game's cursor GetCursorPos/SetCursorPos call sites (`call [import]`,
// FF 15 imm32) read the redirected entries instead. All sites must carry the
// stock bytes or none is changed (sites also in game_sites.h). Verified on game.exe SHA-256 be0fc330...:
// theCursor update 005E538C stores GetCursorPos unchanged as its position;
// theCursor SetPos 005E679F, 005EADBB and the camera handlers 0062FF4A,
// 0062FFFA warp to theCursor+0x78/+0x7C; 0062F07A feeds GetCursorPos to the
// interface. Runtime-library callers that convert themselves are untouched.
void install_game_cursor_coordinates(bool sites_ok){
 static bool attempted=false;if(attempted)return;attempted=true;
 if(!sites_ok){trace("GameCursor.Rejected","cursor site bytes differ in game.exe");return;}
 auto* image=reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
 constexpr std::uint32_t kGetImportRva=0x0033b49c,kSetImportRva=0x0033b4a0;
 struct Site{std::uint32_t rva,import_rva;};
 constexpr Site sites[]={{0x001e538c,kGetImportRva},{0x0022f07a,kGetImportRva},{0x001e679f,kSetImportRva},
  {0x001eadbb,kSetImportRva},{0x0022ff4a,kSetImportRva},{0x0022fffa,kSetImportRva}};
 for(const auto& site:sites){
  std::uint32_t target=0;std::memcpy(&target,image+site.rva+2,4);
  if(image[site.rva]!=0xff||image[site.rva+1]!=0x15||target!=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image+site.import_rva))){
   trace("GameCursor.Rejected","rva=%08lx bytes differ; cursor stays in screen coordinates",(unsigned long)site.rva);return;}
 }
 const owned::GameCursorHooks hooks=owned::game_cursor_hooks(reinterpret_cast<owned::GetCursorPosFn*>(image+kGetImportRva),
  reinterpret_cast<owned::SetCursorPosFn*>(image+kSetImportRva));
 for(const auto& site:sites){
  const std::uint32_t entry=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
   site.import_rva==kGetImportRva?static_cast<void*>(hooks.get_entry):static_cast<void*>(hooks.set_entry)));
  DWORD protection=0,ignored=0;
  if(!VirtualProtect(image+site.rva+2,4,PAGE_EXECUTE_READWRITE,&protection)){trace("GameCursor.Failed","rva=%08lx",(unsigned long)site.rva);continue;}
  std::memcpy(image+site.rva+2,&entry,4);VirtualProtect(image+site.rva+2,4,protection,&ignored);
  FlushInstructionCache(GetCurrentProcess(),image+site.rva,6);
 }
 // Harness runs drive the game without input: keep a parked cursor off the
 // window edges so the game does not edge-scroll the harness camera.
 owned::set_game_cursor_harness(GetEnvironmentVariableA("EI_RENDERER_HARNESS_DIR",nullptr,0)>0);
 trace("GameCursor.Installed","sites=%u",(unsigned)(sizeof(sites)/sizeof(sites[0])));
}

void install_runtime_bridges(){
 window_sampler::start();
 stall_watchdog::start();
 dinput_guard::log_pending();
 fear_watch::install();
 bridge_set_completed_visual_surface_reader(&proxy_completed_visual_surface);
 // Stock widescreen first; foreign layouts reject visibly. The bridge then
 // chains its native HUD, character-pane and camera hooks above ours.
 {const std::wstring ini=owned::renderer_config_path();owned::WidescreenSettings ws{};
  ws.hud=static_cast<owned::widescreen::HudWidth>(std::clamp<int>(static_cast<int>(GetPrivateProfileIntW(L"graphics",L"hud_width",2,ini.c_str())),0,2));
  ws.ui_scale_percent=std::clamp<int>(static_cast<int>(GetPrivateProfileIntW(L"graphics",L"ui_scale",100,ini.c_str())),50,120);
  widescreen_install(owned::game_sites_ok(owned::GameSiteGroup::widescreen),ws);}
 auto& engine=EngineBridge::instance();engine.install();
 const bool mapping_active=install_res_mapping_hooks(owned::game_sites_ok(owned::GameSiteGroup::res_mapping));
 const auto mapping=res_mapping_snapshot();
 trace(mapping_active?"ResMapping.Installed":"ResMapping.Rejected",
       "status=%s validation_failures=%llu",res_mapping_status_name(mapping.status),
       (unsigned long long)mapping.hook_validation_failures);
 install_game_cursor_coordinates(owned::game_sites_ok(owned::GameSiteGroup::cursor_coordinates));
 owned::install_game_focus(owned::game_sites_ok(owned::GameSiteGroup::focus));
}

// Windowed mode: a movable, non-resizable window centred in the work area.
// The game lays its interface out for this client size (SetCooperativeLevel
// reads it below). Live Apply keeps the native raster height separately.
bool set_window_style(HWND window,int slot,LONG_PTR value){
 SetLastError(ERROR_SUCCESS);const auto previous=SetWindowLongPtrA(window,slot,value);
 return previous || GetLastError()==ERROR_SUCCESS;
}
struct OutputWindowPlacement {
 LONG_PTR style{},ex_style{};
 RECT outer{};
 owned::ClientSize client{};
 HWND insert_after{};
};

// Read-only sizing: live admission must finish before changing any window,
// GPU resource or native interface field. Startup uses this same placement.
bool plan_output_window(HWND window,const owned::DisplaySettings& display,OutputWindowPlacement& placement){
 if(!window)return false;
 MONITORINFO info{};info.cbSize=sizeof(info);
 if(!GetMonitorInfoA(MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY),&info))return false;
 const LONG monitor_width=info.rcMonitor.right-info.rcMonitor.left,monitor_height=info.rcMonitor.bottom-info.rcMonitor.top;
 if(monitor_width<=0 || monitor_height<=0)return false;
 placement.style=GetWindowLongPtrA(window,GWL_STYLE);placement.ex_style=GetWindowLongPtrA(window,GWL_EXSTYLE);
 if(display.mode!=owned::DisplayMode::windowed){
  placement.style&=~static_cast<LONG_PTR>(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU);placement.style|=WS_POPUP|WS_VISIBLE;
  placement.ex_style&=~static_cast<LONG_PTR>(WS_EX_WINDOWEDGE|WS_EX_CLIENTEDGE|WS_EX_DLGMODALFRAME|WS_EX_STATICEDGE);
  placement.outer=info.rcMonitor;placement.client={monitor_width,monitor_height};placement.insert_after=HWND_TOP;
  return true;
 }
 placement.style&=~static_cast<LONG_PTR>(WS_POPUP|WS_THICKFRAME|WS_MAXIMIZEBOX);placement.style|=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VISIBLE;
 placement.ex_style&=~static_cast<LONG_PTR>(WS_EX_TOPMOST|WS_EX_CLIENTEDGE|WS_EX_DLGMODALFRAME|WS_EX_STATICEDGE);
 auto user32=GetModuleHandleA("user32.dll");
 using GetDpi=UINT(WINAPI*)(HWND);using AdjustForDpi=BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);
 auto getDpi=reinterpret_cast<GetDpi>(GetProcAddress(user32,"GetDpiForWindow"));
 auto adjustForDpi=reinterpret_cast<AdjustForDpi>(GetProcAddress(user32,"AdjustWindowRectExForDpi"));
 RECT frame{};const DWORD frameStyle=static_cast<DWORD>(placement.style),frameExStyle=static_cast<DWORD>(placement.ex_style);
 if(adjustForDpi&&getDpi){if(!adjustForDpi(&frame,frameStyle,FALSE,frameExStyle,getDpi(window)))return false;}
 else if(!AdjustWindowRectEx(&frame,frameStyle,FALSE,frameExStyle))return false;
 const LONG frameWidth=frame.right-frame.left,frameHeight=frame.bottom-frame.top;
 const RECT& work=info.rcWork;const LONG workWidth=work.right-work.left,workHeight=work.bottom-work.top;
 if(workWidth<=frameWidth || workHeight<=frameHeight)return false;
 placement.client=owned::windowed_client_size(display,monitor_width,monitor_height,workWidth-frameWidth,workHeight-frameHeight);
 const LONG outerWidth=placement.client.width+frameWidth,outerHeight=placement.client.height+frameHeight;
 const LONG x=work.left+(workWidth-outerWidth)/2,y=work.top+(workHeight-outerHeight)/2;
 placement.outer={x,y,x+outerWidth,y+outerHeight};placement.insert_after=HWND_NOTOPMOST;
 return true;
}

bool apply_output_window(HWND window,const OutputWindowPlacement& placement){
 if(!set_window_style(window,GWL_STYLE,placement.style) || !set_window_style(window,GWL_EXSTYLE,placement.ex_style))return false;
 return SetWindowPos(window,placement.insert_after,placement.outer.left,placement.outer.top,
  placement.outer.right-placement.outer.left,placement.outer.bottom-placement.outer.top,
  SWP_FRAMECHANGED|SWP_NOOWNERZORDER|SWP_NOACTIVATE)!=FALSE;
}

// The game hands its window over at SetCooperativeLevel; eid3d11.ini chooses
// borderless fullscreen on the primary monitor (default) or windowed.
bool configure_output_window(HWND window,const owned::DisplaySettings* requested=nullptr){
 if(!window)return false;char processPath[MAX_PATH]{};GetModuleFileNameA(nullptr,processPath,MAX_PATH);const char* name=std::strrchr(processPath,'\\');name=name?name+1:processPath;if(_stricmp(name,"game.exe")!=0)return false;
 auto user32=GetModuleHandleA("user32.dll");using SetThreadDpiContext=HANDLE(WINAPI*)(HANDLE);auto setThread=reinterpret_cast<SetThreadDpiContext>(GetProcAddress(user32,"SetThreadDpiAwarenessContext"));if(setThread)setThread(reinterpret_cast<HANDLE>(-4));
 const owned::DisplaySettings display=requested?*requested:owned::read_display_settings(owned::renderer_config_path().c_str());
 OutputWindowPlacement placement{};if(!plan_output_window(window,display,placement) || !apply_output_window(window,placement))return false;
 owned::set_game_cursor_window(window);
 trace(display.mode==owned::DisplayMode::windowed?"Window.Windowed":"Window.Borderless","requested=%dx%d client=%dx%d",display.window_width,display.window_height,placement.client.width,placement.client.height);
 return true;
}

HANDLE open_exceptions_log(){
 char logPath[MAX_PATH]{};GetModuleFileNameA(g_module,logPath,MAX_PATH);char* slash=std::strrchr(logPath,'\\');if(slash)strcpy_s(slash+1,MAX_PATH-(size_t)(slash+1-logPath),"owned_exceptions.log");
 return CreateFileA(logPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
}
// std::terminate / pure virtual call: one line with the reason, thread, the
// current exception's text if any, and the ddraw.dll RVAs (map base 0x10000000)
// of the return addresses on the stack; then abort as before.
[[noreturn]] void fatal_trace(const char* reason){
 char what[160]="-";
 if(auto current=std::current_exception()){try{std::rethrow_exception(current);}catch(const std::exception& e){strncpy_s(what,e.what(),_TRUNCATE);}catch(...){strcpy_s(what,"non-std exception");}}
 MODULEINFO info{};GetModuleInformation(GetCurrentProcess(),g_module,&info,sizeof(info));
 const auto base=reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
 void* frames[32]{};const USHORT count=RtlCaptureStackBackTrace(1,32,frames,nullptr);
 SYSTEMTIME now{};GetLocalTime(&now);char line[1024]{};
 int used=wsprintfA(line,"%02u:%02u:%02u.%03u %s thread=%lu what=%s ddraw_rva:",now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,reason,GetCurrentThreadId(),what);
 for(USHORT i=0;i<count&&used<980;++i){const auto a=reinterpret_cast<std::uintptr_t>(frames[i]);if(a>=base&&a<base+info.SizeOfImage)used+=wsprintfA(line+used," %06lX",static_cast<unsigned long>(a-base));}
 used+=wsprintfA(line+used,"\r\n");
 HANDLE file=open_exceptions_log();if(file!=INVALID_HANDLE_VALUE){DWORD written{};WriteFile(file,line,(DWORD)used,&written,nullptr);CloseHandle(file);}
 std::abort();
}
[[noreturn]] void terminate_trace(){fatal_trace("terminate");}
void __cdecl purecall_trace(){fatal_trace("purecall");}

// Repeats are capped: each distinct exception (code, module, offset and,
// for a C++ throw, the thrown type) is logged its first 8 times, then at
// each power of two of its count; the whole session writes at most 4096
// lines. A repeat that is not logged returns before any file or stack work.
// Lock-free (a vectored handler can run on any thread, nested in itself).
struct ExceptionTraceKey{volatile LONG64 key;volatile LONG count;};
ExceptionTraceKey g_exception_keys[512]{};
volatile LONG g_exception_lines=0;
constexpr LONG kExceptionLineCap=4096;
// The occurrence count of this key (0: not tracked, the table is full).
LONG exception_trace_count(std::uint64_t key){
 if(!key)key=1; // 0 marks a free slot
 for(unsigned probe=0;probe<std::size(g_exception_keys);++probe){
  auto& slot=g_exception_keys[(static_cast<unsigned>(key)+static_cast<unsigned>(key>>32)+probe)%std::size(g_exception_keys)];
  const LONG64 seen=InterlockedCompareExchange64(&slot.key,static_cast<LONG64>(key),0);
  if(seen==0||seen==static_cast<LONG64>(key))return InterlockedIncrement(&slot.count);
 }
 return 0;
}
// Each line names the faulting module and the offset from its load base
// (ddraw.dll RVAs match the map at 0x10000000+RVA); the first line of a
// session records the game and ddraw.dll load bases.
LONG CALLBACK exception_trace(EXCEPTION_POINTERS* pointers){
 if(!pointers||!pointers->ExceptionRecord)return EXCEPTION_CONTINUE_SEARCH;auto* record=pointers->ExceptionRecord;
 if(record->ExceptionCode!=0xE06D7363u&&record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
 if(g_exception_lines>=kExceptionLineCap)return EXCEPTION_CONTINUE_SEARCH;
 HMODULE owner{};GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(record->ExceptionAddress),&owner);
 LONG occurrence=0;
 {
  const auto offset=owner?static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(record->ExceptionAddress)-reinterpret_cast<std::uintptr_t>(owner)):0u;
  const std::uint64_t thrown=record->ExceptionCode==0xE06D7363u&&record->NumberParameters>2?record->ExceptionInformation[2]:0u;
  std::uint64_t key=(std::uint64_t(record->ExceptionCode)<<32)^offset;
  key^=(std::uint64_t(reinterpret_cast<std::uintptr_t>(owner))*0x9E3779B97F4A7C15ull)^(thrown*0xC2B2AE3D27D4EB4Full);
  occurrence=exception_trace_count(key);
  if(occurrence>8&&(occurrence&(occurrence-1))!=0)return EXCEPTION_CONTINUE_SEARCH;
 }
 if(InterlockedIncrement(&g_exception_lines)>kExceptionLineCap)return EXCEPTION_CONTINUE_SEARCH;
 char modulePath[MAX_PATH]{};if(owner)GetModuleFileNameA(owner,modulePath,MAX_PATH);
 HANDLE file=open_exceptions_log();if(file==INVALID_HANDLE_VALUE)return EXCEPTION_CONTINUE_SEARCH;
 const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
 MODULEINFO self{};GetModuleInformation(GetCurrentProcess(),g_module,&self,sizeof(self));
 const std::uintptr_t ddrawBase=reinterpret_cast<std::uintptr_t>(self.lpBaseOfDll),ddrawEnd=ddrawBase+self.SizeOfImage;
 DWORD written{};char line[512]{};SYSTEMTIME now{};GetLocalTime(&now);
 static LONG sessionLogged=0;
 if(InterlockedExchange(&sessionLogged,1)==0){int length=wsprintfA(line,"%02u:%02u:%02u.%03u session pid=%lu game_base=%08lX ddraw_base=%08lX ddraw_size=%08lX\r\n",now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentProcessId(),static_cast<unsigned long>(image),static_cast<unsigned long>(ddrawBase),static_cast<unsigned long>(self.SizeOfImage));WriteFile(file,line,(DWORD)length,&written,nullptr);}
 // For a C++ throw p2 is the thrown type's ThrowInfo, inside the throwing module.
 ULONG_PTR p0=record->NumberParameters>0?record->ExceptionInformation[0]:0,p1=record->NumberParameters>1?record->ExceptionInformation[1]:0,p2=record->NumberParameters>2?record->ExceptionInformation[2]:0;
 const auto faultOffset=owner?static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(record->ExceptionAddress)-reinterpret_cast<std::uintptr_t>(owner)):0ul;
 int length=wsprintfA(line,"%02u:%02u:%02u.%03u code=%08lX address=%p offset=+%06lX thread=%lu p0=%p p1=%p p2=%p module=%s count=%ld%s\r\n",now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,record->ExceptionCode,record->ExceptionAddress,faultOffset,GetCurrentThreadId(),(void*)p0,(void*)p1,(void*)p2,modulePath,occurrence,g_exception_lines>=kExceptionLineCap?" (session line cap reached)":"");WriteFile(file,line,(DWORD)length,&written,nullptr);
 // For an access violation in game.exe or ddraw.dll, the return addresses
 // found on the faulting stack name the caller chain: game addresses at the
 // 0x400000 image base, ddraw.dll ones as d+RVA.
 if(record->ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&(owner==reinterpret_cast<HMODULE>(image)||owner==g_module)&&pointers->ContextRecord){
  const auto* headers=reinterpret_cast<const IMAGE_NT_HEADERS*>(image+reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
  const std::uintptr_t text_end=image+headers->OptionalHeader.SizeOfImage;
  char trace[1024]{};int used=wsprintfA(trace,"  stack:");int found=0;
  const auto* slot=reinterpret_cast<const std::uintptr_t*>(pointers->ContextRecord->Esp);
  for(int i=0;i<2048&&found<32&&used<960;++i){
   std::uintptr_t value=0;
   __try{value=slot[i];}__except(EXCEPTION_EXECUTE_HANDLER){break;}
   if(value>image+0x1000&&value<text_end){used+=wsprintfA(trace+used," %08lX",static_cast<unsigned long>(value-image+0x400000));++found;}
   else if(value>ddrawBase+0x1000&&value<ddrawEnd){used+=wsprintfA(trace+used," d+%06lX",static_cast<unsigned long>(value-ddrawBase));++found;}
  }
  used+=wsprintfA(trace+used,"\r\n");WriteFile(file,trace,(DWORD)used,&written,nullptr);
 }
 CloseHandle(file);return EXCEPTION_CONTINUE_SEARCH;
}

DDPIXELFORMAT bgra32(){DDPIXELFORMAT f{};f.dwSize=sizeof(f);f.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;f.dwRGBBitCount=32;f.dwRBitMask=0x00ff0000;f.dwGBitMask=0x0000ff00;f.dwBBitMask=0x000000ff;f.dwRGBAlphaBitMask=0xff000000;return f;}
DDPIXELFORMAT rgb_format(DWORD bits,DWORD r,DWORD g,DWORD b,DWORD a=0){DDPIXELFORMAT f{};f.dwSize=sizeof(f);f.dwFlags=DDPF_RGB|(a?DDPF_ALPHAPIXELS:0);f.dwRGBBitCount=bits;f.dwRBitMask=r;f.dwGBitMask=g;f.dwBBitMask=b;f.dwRGBAlphaBitMask=a;return f;}
DDPIXELFORMAT raw_format(DWORD flags,DWORD bits,DWORD r,DWORD g,DWORD b,DWORD a=0){DDPIXELFORMAT f{};f.dwSize=sizeof(f);f.dwFlags=flags;f.dwRGBBitCount=bits;f.dwRBitMask=r;f.dwGBitMask=g;f.dwBBitMask=b;f.dwRGBAlphaBitMask=a;return f;}
DDPIXELFORMAT fourcc_format(DWORD code){DDPIXELFORMAT f{};f.dwSize=sizeof(f);f.dwFlags=DDPF_FOURCC;f.dwFourCC=code;return f;}
bool block_compressed(const DDPIXELFORMAT& f){return (f.dwFlags&DDPF_FOURCC)&&(f.dwFourCC==MAKEFOURCC('D','X','T','1')||f.dwFourCC==MAKEFOURCC('D','X','T','2')||f.dwFourCC==MAKEFOURCC('D','X','T','3')||f.dwFourCC==MAKEFOURCC('D','X','T','4')||f.dwFourCC==MAKEFOURCC('D','X','T','5'));}
DWORD block_bytes(const DDPIXELFORMAT& f){return f.dwFourCC==MAKEFOURCC('D','X','T','1')?8:16;}
DWORD pixel_bytes(const DDPIXELFORMAT& f){if(f.dwFlags&DDPF_ZBUFFER)return std::max<DWORD>(1,f.dwZBufferBitDepth/8);return std::max<DWORD>(1,f.dwRGBBitCount/8);}
LONG surface_pitch(UINT width,const DDPIXELFORMAT& f){return block_compressed(f)?(LONG)(std::max<UINT>(1,(width+3)/4)*block_bytes(f)):(LONG)width*(LONG)pixel_bytes(f);}
UINT surface_rows(UINT height,const DDPIXELFORMAT& f){return block_compressed(f)?std::max<UINT>(1,(height+3)/4):height;}
BYTE expand_mask(DWORD raw,DWORD mask,BYTE fallback){if(!mask)return fallback;DWORD shift=0;while(((mask>>shift)&1)==0)++shift;DWORD value=(raw&mask)>>shift,maximum=mask>>shift;return maximum?(BYTE)((value*255u+maximum/2)/maximum):fallback;}
DWORD pack_mask(BYTE value,DWORD mask){if(!mask)return 0;DWORD shift=0;while(((mask>>shift)&1)==0)++shift;DWORD maximum=mask>>shift;return ((DWORD(value)*maximum+127)/255<<shift)&mask;}
DDPIXELFORMAT z24s8(){DDPIXELFORMAT f{};f.dwSize=sizeof(f);f.dwFlags=DDPF_ZBUFFER|DDPF_STENCILBUFFER;f.dwZBufferBitDepth=32;f.dwZBitMask=0x00ffffff;f.dwStencilBitDepth=8;f.dwStencilBitMask=0xff000000;return f;}
void fill_d3d_caps(D3DDEVICEDESC7& d){
 d={};d.deviceGUID=IID_IDirect3DHALDevice;
 d.dwDevCaps=D3DDEVCAPS_DRAWPRIMTLVERTEX|D3DDEVCAPS_HWRASTERIZATION|D3DDEVCAPS_TEXTUREVIDEOMEMORY;
 d.dpcTriCaps.dwSize=sizeof(d.dpcTriCaps);
 d.dpcTriCaps.dwMiscCaps=D3DPMISCCAPS_CULLNONE|D3DPMISCCAPS_CULLCW|D3DPMISCCAPS_CULLCCW;
 d.dpcTriCaps.dwRasterCaps=D3DPRASTERCAPS_DITHER|D3DPRASTERCAPS_FOGVERTEX|D3DPRASTERCAPS_ZTEST;
 d.dpcTriCaps.dwZCmpCaps=D3DPCMPCAPS_NEVER|D3DPCMPCAPS_LESS|D3DPCMPCAPS_EQUAL|D3DPCMPCAPS_LESSEQUAL|D3DPCMPCAPS_GREATER|D3DPCMPCAPS_NOTEQUAL|D3DPCMPCAPS_GREATEREQUAL|D3DPCMPCAPS_ALWAYS;
 d.dpcTriCaps.dwSrcBlendCaps=d.dpcTriCaps.dwDestBlendCaps=D3DPBLENDCAPS_ZERO|D3DPBLENDCAPS_ONE|D3DPBLENDCAPS_SRCCOLOR|D3DPBLENDCAPS_INVSRCCOLOR|D3DPBLENDCAPS_SRCALPHA|D3DPBLENDCAPS_INVSRCALPHA|D3DPBLENDCAPS_DESTALPHA|D3DPBLENDCAPS_INVDESTALPHA|D3DPBLENDCAPS_DESTCOLOR|D3DPBLENDCAPS_INVDESTCOLOR;
 d.dpcTriCaps.dwAlphaCmpCaps=d.dpcTriCaps.dwZCmpCaps;
 d.dpcTriCaps.dwTextureCaps=D3DPTEXTURECAPS_ALPHA|D3DPTEXTURECAPS_PERSPECTIVE;
 d.dpcTriCaps.dwTextureFilterCaps=D3DPTFILTERCAPS_MINFPOINT|D3DPTFILTERCAPS_MINFLINEAR|D3DPTFILTERCAPS_MAGFPOINT|D3DPTFILTERCAPS_MAGFLINEAR;
 d.dpcTriCaps.dwTextureAddressCaps=D3DPTADDRESSCAPS_WRAP|D3DPTADDRESSCAPS_CLAMP|D3DPTADDRESSCAPS_MIRROR;
 d.dwDeviceRenderBitDepth=DDBD_32;d.dwDeviceZBufferBitDepth=DDBD_32;
 d.dwMinTextureWidth=d.dwMinTextureHeight=1;d.dwMaxTextureWidth=d.dwMaxTextureHeight=8192;
 d.dwMaxTextureRepeat=8192;d.dwMaxTextureAspectRatio=8192;d.wMaxTextureBlendStages=1;d.wMaxSimultaneousTextures=1;
}
}

class DD;
class Surface;
// Any live proxy surface's vtable; identifies a game-held pointer as ours.
void* g_surface_vtable=nullptr;
// Live surfaces by their unique id; resolves completed visual sources to textures.
std::unordered_map<std::uint64_t,Surface*> g_surfaces_by_id;
// Surfaces currently holding a GDI DIB section (swept once per present).
std::vector<Surface*> g_dc_holders;

class Surface final:public IDirectDrawSurface7 {
public:
 Surface(DD* owner,const DDSURFACEDESC2& desc);
 ~Surface();
 SurfaceImage* image(){return &image_;} HRESULT ensurePixels(); bool primary()const{return (desc_.ddsCaps.dwCaps&DDSCAPS_PRIMARYSURFACE)!=0;} bool compositionOutput()const noexcept; DD* owner()const{return owner_;}
 bool completedVisualValue(std::shared_ptr<const OwnedVisualSurface>& value) noexcept;
 bool publishCampIcon(const PreparedCampIcon& icon,
  std::shared_ptr<const OwnedVisualSurface>& value) noexcept;
 void completedVisualMutation() noexcept;
 bool visualCpuCurrent() const noexcept;
 void invalidateVisualCpu() noexcept;
 void invalidateVisualTarget() noexcept;
 void completedVisualCpuWrite(bool full_surface, bool source_current) noexcept;
 HRESULT legacyCpuAccess();
 bool displayResizeIdle() const noexcept {return !locked_ && !dc_active_ && !image_.cpu_access_active;}
 void resizeDisplayCanvas(UINT width,UINT height) noexcept {
  freeDcStorage();image_.pixels=SurfacePixels{};image_.texture.Reset();image_.srv.Reset();
  image_.width=desc_.dwWidth=width;image_.height=desc_.dwHeight=height;
  image_.pitch=desc_.lPitch=surface_pitch(width,image_.format);image_.gpu_captured=false;image_.cpu_released=false;
  mark_surface_dirty_full(image_);advance_surface_generation(image_,nullptr);update_surface_memory_accounting(image_);
  completedVisualMutation();invalidateVisualCpu();++unique_;
 }
 Surface* displayDepth() const noexcept {return attached_ && (attached_->desc_.ddsCaps.dwCaps&DDSCAPS_ZBUFFER)?attached_:nullptr;}
 // GPU-primary residency: restore a released texture's pixels for a CPU
 // reader (pin retains known CPU inputs), drop draw-only bytes after the
 // sampling grace, and upload a finished CPU write session at once.
 HRESULT restoreReleased(bool pin=false);
 void releaseCpuPixels() noexcept;
 void finishCpuWrite() noexcept;
 HRESULT ensurePixelsForWrite(bool full_overwrite);
 // Frees the DIB section of a surface whose DC was not taken recently.
 void sweepDcStorage() noexcept{if(dc_&&!dc_active_&&g_present_serial-dc_last_present_>kDcReuseFrames)freeDcStorage();}
#ifdef EID3D11_TESTING
 friend bool test_completed_visual_surface_contract() noexcept;
#endif
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override;
 ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
 ULONG STDMETHODCALLTYPE Release() override{ULONG n=--refs_;if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE7) override;
 HRESULT STDMETHODCALLTYPE AddOverlayDirtyRect(LPRECT) override{return unsupported("Surface.AddOverlayDirtyRect");}
 HRESULT STDMETHODCALLTYPE Blt(LPRECT,LPDIRECTDRAWSURFACE7,LPRECT,DWORD,LPDDBLTFX) override;
 HRESULT STDMETHODCALLTYPE BltBatch(LPDDBLTBATCH,DWORD,DWORD) override{return unsupported("Surface.BltBatch");}
 HRESULT STDMETHODCALLTYPE BltFast(DWORD,DWORD,LPDIRECTDRAWSURFACE7,LPRECT,DWORD) override;
 HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD,LPDIRECTDRAWSURFACE7) override;
 HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID,LPDDENUMSURFACESCALLBACK7) override;
 HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD,LPVOID,LPDDENUMSURFACESCALLBACK7) override{return unsupported("Surface.EnumOverlayZOrders");}
 HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE7,DWORD) override;
 HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS2,LPDIRECTDRAWSURFACE7*) override;
 HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD) override{trace("Surface.GetBltStatus");return DD_OK;}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS2) override;
 HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER*) override{return unsupported("Surface.GetClipper");}
 HRESULT STDMETHODCALLTYPE GetColorKey(DWORD,LPDDCOLORKEY) override;
 HRESULT STDMETHODCALLTYPE GetDC(HDC*) override;
 HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD) override{trace("Surface.GetFlipStatus");return DD_OK;}
 HRESULT STDMETHODCALLTYPE GetOverlayPosition(LPLONG,LPLONG) override{return unsupported("Surface.GetOverlayPosition");}
 HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE*) override{return unsupported("Surface.GetPalette");}
 HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT) override;
 HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC2) override;
 HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW,LPDDSURFACEDESC2) override{return DDERR_ALREADYINITIALIZED;}
 HRESULT STDMETHODCALLTYPE IsLost() override{trace("Surface.IsLost");return lost_?DDERR_SURFACELOST:DD_OK;}
 HRESULT STDMETHODCALLTYPE Lock(LPRECT,LPDDSURFACEDESC2,DWORD,HANDLE) override;
 HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override;
 HRESULT STDMETHODCALLTYPE Restore() override{trace("Surface.Restore");lost_=false;completedVisualMutation();return DD_OK;}
 HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER) override{return unsupported("Surface.SetClipper");}
 HRESULT STDMETHODCALLTYPE SetColorKey(DWORD,LPDDCOLORKEY) override;
 HRESULT STDMETHODCALLTYPE SetOverlayPosition(LONG,LONG) override{return unsupported("Surface.SetOverlayPosition");}
 HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE) override{return unsupported("Surface.SetPalette");}
 HRESULT STDMETHODCALLTYPE Unlock(LPRECT) override;
 HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT,LPDIRECTDRAWSURFACE7,LPRECT,DWORD,LPDDOVERLAYFX) override{return unsupported("Surface.UpdateOverlay");}
 HRESULT STDMETHODCALLTYPE UpdateOverlayDisplay(DWORD) override{return unsupported("Surface.UpdateOverlayDisplay");}
 HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD,LPDIRECTDRAWSURFACE7) override{return unsupported("Surface.UpdateOverlayZOrder");}
 HRESULT STDMETHODCALLTYPE GetDDInterface(LPVOID*) override;
 HRESULT STDMETHODCALLTYPE PageLock(DWORD) override{trace("Surface.PageLock");return DD_OK;}
 HRESULT STDMETHODCALLTYPE PageUnlock(DWORD) override{trace("Surface.PageUnlock");return DD_OK;}
 HRESULT STDMETHODCALLTYPE SetSurfaceDesc(LPDDSURFACEDESC2,DWORD) override{return unsupported("Surface.SetSurfaceDesc");}
 HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,LPVOID,DWORD,DWORD) override{return unsupported("Surface.SetPrivateData");}
 HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,LPVOID,LPDWORD) override{return unsupported("Surface.GetPrivateData");}
 HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override{return unsupported("Surface.FreePrivateData");}
 HRESULT STDMETHODCALLTYPE GetUniquenessValue(LPDWORD value) override{if(!value)return DDERR_INVALIDPARAMS;*value=unique_;return DD_OK;}
 HRESULT STDMETHODCALLTYPE ChangeUniquenessValue() override{++unique_;return DD_OK;}
 HRESULT STDMETHODCALLTYPE SetPriority(DWORD v) override{priority_=v;return DD_OK;}
 HRESULT STDMETHODCALLTYPE GetPriority(LPDWORD v) override{if(!v)return DDERR_INVALIDPARAMS;*v=priority_;return DD_OK;}
 HRESULT STDMETHODCALLTYPE SetLOD(DWORD v) override{lod_=v;return DD_OK;}
 HRESULT STDMETHODCALLTYPE GetLOD(LPDWORD v) override{if(!v)return DDERR_INVALIDPARAMS;*v=lod_;return DD_OK;}
private:
 HRESULT unsupported(const char* n){trace(n);return DDERR_UNSUPPORTED;}
 HRESULT ensureDcStorage();
 HRESULT ensureOwnedTextDc();
 // Owned interface layer (owned_ui_layers.h): a CI3DSurface DC session
 // answered with a recording DC. The surface's pixels change only when its
 // recipe is replayed onto the DIB of a DC session.
 bool ownedLayerClaim(const void* caller);
 HRESULT beginDibSession();
 bool packDibSession(RECT* changed);
 bool packDibRect(const RECT& area,RECT& changed);
 void finishLayerWrite(bool changed,const RECT& rect);
 void omitLayer(UiLayerFailure failure);
 HRESULT releaseLayerDc(HDC value);
 static HDC promoteLayer(void* context,const std::uint8_t* recipe,std::size_t size) noexcept;
 bool dc_layer_ = false;
 bool layer_promoted_ = false;
 HDC layer_dc_ = nullptr;
 // Generation whose pixels the DIB holds exactly as a full unpack would
 // give them (0: unknown); a replay on a current DIB skips the unpack.
 std::uint64_t dib_generation_ = 0;
 UiLayerState layer_state_;
 std::vector<std::uint8_t> layer_recipe_;
 bool visual_source_observed_ = false;
 // The current generation's publication, held while this surface exists so a
 // unit that leaves and re-enters the scene finds it again instead of
 // reading the released pixels back from the GPU. Dropped on rejection,
 // invalidation or a new generation's publication; destruction releases it.
 std::shared_ptr<const OwnedVisualSurface> held_publication_;
 bool visual_cpu_current_ = true;
 std::uint64_t dc_last_present_ = 0;
 void freeDcStorage() noexcept;
 std::atomic<ULONG> refs_{1};DD* owner_{};DDSURFACEDESC2 desc_{};SurfaceImage image_{};Surface* attached_{};HDC dc_{};HBITMAP dc_bitmap_{};HGDIOBJ dc_old_bitmap_{};void* dc_bits_{};HDC owned_text_dc_{};HBITMAP owned_text_bitmap_{};HGDIOBJ owned_text_old_bitmap_{};void* owned_text_bits_{};RECT locked_rect_{};DWORD locked_flags_{};HudAtlasMutationSnapshot hud_atlas_snapshot_{};bool dc_active_{};bool dc_owned_text_{};bool lost_{};bool locked_{};bool has_key_{};DDCOLORKEY key_{};DWORD unique_{1},priority_{},lod_{};
};

HRESULT resolve_proxy_surface_texture(void* context,ID3D11Device*,std::uint64_t id,std::uint64_t generation,ComPtr<ID3D11Texture2D>& texture,SurfaceImage*& image_out);
// Scene thread (render_thread option): the renderer resolves completed visual
// sources on the scene thread, which never touches a proxy Surface. The game
// thread registers each published generation's GPU home here (uploaded on the
// game thread); a miss is requested back and serviced at the next present.
struct SceneSurfaceTexture{std::uint64_t generation=0;ComPtr<ID3D11Texture2D> texture;};
std::mutex g_scene_surface_lock;
std::unordered_map<std::uint64_t,SceneSurfaceTexture> g_scene_surface_textures;
std::vector<std::pair<std::uint64_t,std::uint64_t>> g_scene_surface_requests;
void scene_surface_texture_erase(std::uint64_t id) noexcept{
 std::lock_guard<std::mutex> guard(g_scene_surface_lock);g_scene_surface_textures.erase(id);
}
class Device;
class DD final:public IDirectDraw7,public IDirect3D7 {
public:
 DD(){trace("DD.Create");owned_resource_runtime_acquire(EngineBridge::instance().resource_registry_key());renderer.set_surface_texture_resolver(&resolve_proxy_surface_texture,this);}
 ~DD(){trace("DD.Destroy");renderer.release();owned_resource_runtime_release();}
 Renderer renderer; HWND hwnd{};DWORD width{800},height{600},bpp{32},coop{};Surface* primary{};Surface* active_target{};Device* active_device{};
 // Device removal (driver timeout/TDR). The game treats a failed present,
 // blit or lock as fatal: game.exe 480080 shows "Can't flip render surface"
 // and calls exit(-1), whose CRT destructors then call a UI screen it has
 // already freed (5F7F80 via 667BA0). So a failed call that finds the device
 // removed never returns to the game: it logs once, tells the player and
 // ends the process. A working device only adds one FAILED() test per call.
 HRESULT checkDevice(HRESULT hr){
  if(FAILED(hr)){const HRESULT reason=renderer.device_removed_reason();if(FAILED(reason))deviceRemovedExit(reason);}
  return hr;
 }
 [[noreturn]] void deviceRemovedExit(HRESULT reason) noexcept;
 bool startup_display_pending=true;
 void applyDisplay(const owned::DisplaySettings& settings);
 void invalidateVisualOutput() noexcept{if(active_target)active_target->invalidateVisualCpu();if(primary&&primary!=active_target)primary->invalidateVisualCpu();}
 HRESULT flushVisualUi(){if(renderer.has_pending_ui())invalidateVisualOutput();else if(renderer.scene_queue())return DD_OK;return renderer.flush_ui();}
 void serviceSceneSurfaceRequests() noexcept;
 // Renderer-owned cursor: the game's current frame, extracted from its strip
 // and scaled once per (strip content, frame, scale), drawn after all UI.
 std::uint64_t owned_cursor_key=0;std::vector<std::uint32_t> owned_cursor_pixels;UINT owned_cursor_width=0,owned_cursor_height=0;
 bool owned_cursor_reported=false;
 void reportOwnedCursor(const char* reason){if(owned_cursor_reported)return;owned_cursor_reported=true;owned::bridge_append_probe_event("owned_cursor_omitted",reason);}
 static Surface* ownedCursorSurface(void* pointer){
  __try{if(!pointer||!g_surface_vtable||*static_cast<void* const*>(pointer)!=g_surface_vtable)return nullptr;}
  __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
  return static_cast<Surface*>(static_cast<IDirectDrawSurface7*>(pointer));
 }
 void queueOwnedCursor(){
  // Publication is per presentation, including an absent native cursor.
  renderer.queue_cursor(0,owned_cursor_pixels,0,0,RECT{});
  // While the loading picture is held the same frame is the system cursor
  // (cursor_image.h); every other present, and any frame that cannot be
  // shown, returns to the owned cursor and the game's hidden system cursor.
  const bool load_cursor=renderer.load_cursor_wanted();bool load_cursor_shown=false;
  struct LoadCursorScope{bool& shown;~LoadCursorScope(){if(!shown)owned::hide_load_cursor();}} load_cursor_scope{load_cursor_shown};
  if(video_picture_on_screen())return; // no cursor over a playing movie
  OwnedCursorFrame frame{};if(!bridge_owned_cursor_frame(frame)||!frame.visible)return;
  Surface* strip=ownedCursorSurface(frame.surface);if(!strip)return reportOwnedCursor("strip_not_a_proxy_surface");
  const SurfaceImage& image=*strip->image();
  const int percent=std::clamp(renderer.cursor_scale_percent(),100,400);
  std::uint64_t key=image.id*0x9e3779b97f4a7c15ull^image.generation;
  for(const std::uint64_t value:{(std::uint64_t)frame.source.left,(std::uint64_t)frame.source.top,(std::uint64_t)frame.source.right,(std::uint64_t)frame.source.bottom,(std::uint64_t)percent})
   key=(key^value)*0x100000001b3ull;
  key^=0x43555253ull<<32;
  if(key!=owned_cursor_key){
   // Read on every cursor frame change: restore once and pin CPU-resident.
   if(FAILED(strip->restoreReleased(true)))return reportOwnedCursor("strip_restore_failed");
   if(image.pixels.empty()||frame.source.left<0||frame.source.top<0||frame.source.right>(LONG)image.width||frame.source.bottom>(LONG)image.height)
    return reportOwnedCursor("strip_pixels_unavailable");
   CursorFrameSource source{};source.pixels=image.pixels.data();source.pitch=(std::uint32_t)image.pitch;source.bits=image.format.dwRGBBitCount;
   source.red_mask=image.format.dwRBitMask;source.green_mask=image.format.dwGBitMask;source.blue_mask=image.format.dwBBitMask;
   source.left=(std::uint32_t)frame.source.left;source.top=(std::uint32_t)frame.source.top;
   source.width=(std::uint32_t)(frame.source.right-frame.source.left);source.height=(std::uint32_t)(frame.source.bottom-frame.source.top);
   std::vector<std::uint32_t> base;
   if(!extract_cursor_frame(source,base))return reportOwnedCursor("strip_format_unsupported");
   std::uint32_t width=0,height=0;
   upscale_cursor_frame(base,source.width,source.height,percent/100.f,owned_cursor_pixels,width,height);
   owned_cursor_width=width;owned_cursor_height=height;owned_cursor_key=key;
  }
  // Scale about the hotspot so the click point stays under the same pixel.
  const float scale=percent/100.f;
  const POINT hotspot=owned::game_native_to_output(hwnd,{frame.x+frame.hot_x,frame.y+frame.hot_y});
  const LONG hot_x=hotspot.x,hot_y=hotspot.y;
  const LONG left=hot_x-(LONG)std::lround(frame.hot_x*scale),top=hot_y-(LONG)std::lround(frame.hot_y*scale);
  if(load_cursor){
   if(owned::show_load_cursor(owned_cursor_key,owned_cursor_pixels,owned_cursor_width,owned_cursor_height,
       (std::uint32_t)(std::max)(0L,hot_x-left),(std::uint32_t)(std::max)(0L,hot_y-top))){load_cursor_shown=true;return;}
   reportOwnedCursor("load_cursor_create_failed"); // the owned cursor draws on the held frames instead
  }
  renderer.queue_cursor(owned_cursor_key,owned_cursor_pixels,owned_cursor_width,owned_cursor_height,
   RECT{left,top,left+(LONG)owned_cursor_width,top+(LONG)owned_cursor_height});
 }
 HRESULT presentVisual(){
  ++g_present_serial;
  if(renderer.scene_queue()){serviceSceneSurfaceRequests();bridge_update_combat_portraits();}
  for(std::size_t i=g_dc_holders.size();i-->0;)g_dc_holders[i]->sweepDcStorage();
  queueOwnedCursor();
  PostEngineSurfaceTimingScope timing(PostEngineSurfaceTimingKind::proxy);invalidateVisualOutput();timing.finish();const HRESULT hr=checkDevice(renderer.present());
  if(FAILED(hr)){static unsigned reported=0;if(reported<8){++reported;char detail[96];std::snprintf(detail,sizeof(detail),"hr=%08lx static_draws=%zu",
   static_cast<unsigned long>(hr),renderer.native_static_draw_count());owned::bridge_append_probe_event("present_failed",detail);}}
  if(SUCCEEDED(hr)){
   owned::DisplaySettings requested;
   if(renderer.consume_display_apply(requested)){
    startup_display_pending=false;applyDisplay(requested);
   }else if(startup_display_pending&&active_device&&!owned::widescreen_display_blocker()){
    // The native interface has now initialized against its render surface.
    // Apply the saved output aspect through the same completed-frame path as
    // live Apply, including native bounds, viewport and paired input mapping.
    startup_display_pending=false;
    applyDisplay(owned::read_display_settings(owned::renderer_config_path().c_str()));
   }
  }
  // DirectDraw callers treat any non-zero Flip result as failure; DXGI success
  // statuses (DXGI_STATUS_OCCLUDED while a secure desktop or UAC prompt covers
  // the window) are still a successful present.
  return SUCCEEDED(hr)?DD_OK:hr;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** out) override{
  trace_iid("DD.QueryInterface",riid);if(!out)return E_POINTER;*out=nullptr;
  if(riid==IID_IUnknown||riid==IID_IDirectDraw7)*out=static_cast<IDirectDraw7*>(this);
  else if(riid==IID_IDirect3D7)*out=static_cast<IDirect3D7*>(this);else return E_NOINTERFACE;AddRef();return S_OK;
 }
 ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
 ULONG STDMETHODCALLTYPE Release() override{ULONG n=--refs_;if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Compact() override{trace("DD.Compact");return DD_OK;}
 HRESULT STDMETHODCALLTYPE CreateClipper(DWORD,LPDIRECTDRAWCLIPPER*,IUnknown*) override{return unsupported("DD.CreateClipper");}
 HRESULT STDMETHODCALLTYPE CreatePalette(DWORD,LPPALETTEENTRY,LPDIRECTDRAWPALETTE*,IUnknown*) override{return unsupported("DD.CreatePalette");}
 HRESULT STDMETHODCALLTYPE CreateSurface(LPDDSURFACEDESC2,LPDIRECTDRAWSURFACE7*,IUnknown*) override;
 HRESULT STDMETHODCALLTYPE DuplicateSurface(LPDIRECTDRAWSURFACE7,LPDIRECTDRAWSURFACE7*) override;
 HRESULT STDMETHODCALLTYPE EnumDisplayModes(DWORD,LPDDSURFACEDESC2,LPVOID,LPDDENUMMODESCALLBACK2) override;
 HRESULT STDMETHODCALLTYPE EnumSurfaces(DWORD,LPDDSURFACEDESC2,LPVOID,LPDDENUMSURFACESCALLBACK7) override{return unsupported("DD.EnumSurfaces");}
 HRESULT STDMETHODCALLTYPE FlipToGDISurface() override{
  // DXGI owns windowed presentation; GDI is already available to the game's
  // error dialog. A rejected mode switch recursively reports another error.
  trace("DD.FlipToGDISurface");return DD_OK;
 }
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDCAPS driver,LPDDCAPS hel) override;
 HRESULT STDMETHODCALLTYPE GetDisplayMode(LPDDSURFACEDESC2) override;
 HRESULT STDMETHODCALLTYPE GetFourCCCodes(LPDWORD count,LPDWORD) override{trace("DD.GetFourCCCodes");if(!count)return DDERR_INVALIDPARAMS;*count=0;return DD_OK;}
 HRESULT STDMETHODCALLTYPE GetGDISurface(LPDIRECTDRAWSURFACE7* out) override;
 HRESULT STDMETHODCALLTYPE GetMonitorFrequency(LPDWORD v) override{if(!v)return DDERR_INVALIDPARAMS;*v=60;return DD_OK;}
 HRESULT STDMETHODCALLTYPE GetScanLine(LPDWORD v) override{if(!v)return DDERR_INVALIDPARAMS;*v=0;return DD_OK;}
 HRESULT STDMETHODCALLTYPE GetVerticalBlankStatus(LPBOOL v) override{if(!v)return DDERR_INVALIDPARAMS;*v=FALSE;return DD_OK;}
 HRESULT STDMETHODCALLTYPE Initialize(GUID*) override{return DDERR_ALREADYINITIALIZED;}
 HRESULT STDMETHODCALLTYPE RestoreDisplayMode() override{trace("DD.RestoreDisplayMode");return DD_OK;}
 HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND w,DWORD flags) override{if(w)configure_output_window(w);RECT client{};LONG cw=0,ch=0;if(w&&GetClientRect(w,&client)){cw=client.right-client.left;ch=client.bottom-client.top;if(cw>0&&ch>0){width=(DWORD)cw;height=(DWORD)ch;}}trace("DD.SetCooperativeLevel","flags=%08lx client=%ldx%ld active=%lux%lu",flags,cw,ch,width,height);owned::widescreen_set_surface(width,height);hwnd=w;coop=flags;return DD_OK;}
 HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD w,DWORD h,DWORD bits,DWORD,DWORD) override{trace("DD.SetDisplayMode","%lux%lux%lu",w,h,bits);width=w;height=h;bpp=bits;return DD_OK;}
 HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(DWORD,HANDLE) override{trace("DD.WaitForVerticalBlank");Sleep(1);return DD_OK;}
 HRESULT STDMETHODCALLTYPE GetAvailableVidMem(LPDDSCAPS2,LPDWORD total,LPDWORD free) override{if(total)*total=512u<<20;if(free)*free=384u<<20;return DD_OK;}
 HRESULT STDMETHODCALLTYPE GetSurfaceFromDC(HDC,LPDIRECTDRAWSURFACE7*) override{return unsupported("DD.GetSurfaceFromDC");}
 HRESULT STDMETHODCALLTYPE RestoreAllSurfaces() override{trace("DD.RestoreAllSurfaces");return DD_OK;}
 HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override{return DD_OK;}
 HRESULT STDMETHODCALLTYPE GetDeviceIdentifier(LPDDDEVICEIDENTIFIER2 id,DWORD) override;
 HRESULT STDMETHODCALLTYPE StartModeTest(LPSIZE,DWORD,DWORD) override{return unsupported("DD.StartModeTest");}
 HRESULT STDMETHODCALLTYPE EvaluateMode(DWORD,DWORD*) override{return unsupported("DD.EvaluateMode");}
 // IDirect3D7 shares IUnknown above.
 HRESULT STDMETHODCALLTYPE EnumDevices(LPD3DENUMDEVICESCALLBACK7,LPVOID) override;
 HRESULT STDMETHODCALLTYPE CreateDevice(REFCLSID,LPDIRECTDRAWSURFACE7,LPDIRECT3DDEVICE7*) override;
 HRESULT STDMETHODCALLTYPE CreateVertexBuffer(LPD3DVERTEXBUFFERDESC,LPDIRECT3DVERTEXBUFFER7*,DWORD) override{return d3unsupported("D3D7.CreateVertexBuffer");}
 HRESULT STDMETHODCALLTYPE EnumZBufferFormats(REFCLSID,LPD3DENUMPIXELFORMATSCALLBACK,LPVOID) override;
 HRESULT STDMETHODCALLTYPE EvictManagedTextures() override{trace("D3D7.EvictManagedTextures");return D3D_OK;}
private:
 HRESULT unsupported(const char* n){trace(n);return DDERR_UNSUPPORTED;}
 HRESULT d3unsupported(const char* n){trace(n);return D3DERR_UNSUPPORTEDCOLOROPERATION;}
 std::atomic<ULONG> refs_{1};
};

class LegacySurface final:public IDirectDrawSurface,public IDirectDrawSurface2,public IDirectDrawSurface3,public IDirectDrawSurface4 {
public:
 explicit LegacySurface(Surface* core):core_(core){trace("LegacySurface.Create");}
 ~LegacySurface(){core_->Release();trace("LegacySurface.Destroy");}
 Surface* core()const{return core_;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r,void** out) override{
  trace_iid("LegacySurface.QueryInterface",r);if(!out)return E_POINTER;*out=nullptr;
  if(r==IID_IUnknown||r==IID_IDirectDrawSurface)*out=static_cast<IDirectDrawSurface*>(this);
  else if(r==IID_IDirectDrawSurface2)*out=static_cast<IDirectDrawSurface2*>(this);
  else if(r==IID_IDirectDrawSurface3)*out=static_cast<IDirectDrawSurface3*>(this);
  else if(r==IID_IDirectDrawSurface4)*out=static_cast<IDirectDrawSurface4*>(this);
  else return core_->QueryInterface(r,out);
  AddRef();return S_OK;
 }
 ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
 ULONG STDMETHODCALLTYPE Release() override{ULONG n=--refs_;if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE s) override{auto* p=dynamic_cast<LegacySurface*>(s);return p?core_->AddAttachedSurface(p->core()):DDERR_INVALIDOBJECT;}
 HRESULT STDMETHODCALLTYPE AddOverlayDirtyRect(LPRECT r) override{return core_->AddOverlayDirtyRect(r);}
 HRESULT STDMETHODCALLTYPE Blt(LPRECT a,LPDIRECTDRAWSURFACE b,LPRECT c,DWORD d,LPDDBLTFX e) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->Blt(a,p?p->core():nullptr,c,d,e);}
 HRESULT STDMETHODCALLTYPE BltBatch(LPDDBLTBATCH a,DWORD b,DWORD c) override{return core_->BltBatch(a,b,c);}
 HRESULT STDMETHODCALLTYPE BltFast(DWORD a,DWORD b,LPDIRECTDRAWSURFACE c,LPRECT d,DWORD e) override{auto* p=dynamic_cast<LegacySurface*>(c);return core_->BltFast(a,b,p?p->core():nullptr,d,e);}
 HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD a,LPDIRECTDRAWSURFACE b) override{auto* p=dynamic_cast<LegacySurface*>(b);return p?core_->DeleteAttachedSurface(a,p->core()):DDERR_INVALIDOBJECT;}
 HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID,LPDDENUMSURFACESCALLBACK) override{return unsupported("LegacySurface.EnumAttachedSurfaces");}
 HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD,LPVOID,LPDDENUMSURFACESCALLBACK) override{return unsupported("LegacySurface.EnumOverlayZOrders");}
 HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE a,DWORD b) override{auto* p=dynamic_cast<LegacySurface*>(a);return core_->Flip(p?p->core():nullptr,b);}
 HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS caps,LPDIRECTDRAWSURFACE* out) override{
  trace("LegacySurface.GetAttachedSurface");if(!out)return DDERR_INVALIDPARAMS;*out=nullptr;DDSCAPS2 c{};if(caps)c.dwCaps=caps->dwCaps;LPDIRECTDRAWSURFACE7 found{};HRESULT hr=core_->GetAttachedSurface(caps?&c:nullptr,&found);if(FAILED(hr))return hr;auto* s=dynamic_cast<Surface*>(found);if(!s){found->Release();return DDERR_INVALIDOBJECT;}auto* wrapped=new(std::nothrow)LegacySurface(s);if(!wrapped){found->Release();return DDERR_OUTOFMEMORY;}*out=wrapped;return DD_OK;
 }
 HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD a) override{return core_->GetBltStatus(a);}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS c) override{if(!c)return DDERR_INVALIDPARAMS;DDSCAPS2 c2{};HRESULT hr=core_->GetCaps(&c2);if(SUCCEEDED(hr))c->dwCaps=c2.dwCaps;return hr;}
 HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER* a) override{return core_->GetClipper(a);}
 HRESULT STDMETHODCALLTYPE GetColorKey(DWORD a,LPDDCOLORKEY b) override{return core_->GetColorKey(a,b);}
 HRESULT STDMETHODCALLTYPE GetDC(HDC* a) override{return core_->GetDC(a);}
 HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD a) override{return core_->GetFlipStatus(a);}
 HRESULT STDMETHODCALLTYPE GetOverlayPosition(LPLONG a,LPLONG b) override{return core_->GetOverlayPosition(a,b);}
 HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE* a) override{return core_->GetPalette(a);}
 HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT a) override{return core_->GetPixelFormat(a);}
 HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC d) override{
  trace("LegacySurface.GetSurfaceDesc");if(!d)return DDERR_INVALIDPARAMS;DDSURFACEDESC2 d2{};d2.dwSize=sizeof(d2);HRESULT hr=core_->GetSurfaceDesc(&d2);if(FAILED(hr))return hr;DWORD size=d->dwSize;std::memset(d,0,sizeof(*d));std::memcpy(d,&d2,std::min<size_t>(sizeof(*d),sizeof(d2)));d->dwSize=size?size:sizeof(*d);d->ddsCaps.dwCaps=d2.ddsCaps.dwCaps;return DD_OK;
 }
 HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW,LPDDSURFACEDESC) override{return DDERR_ALREADYINITIALIZED;}
 HRESULT STDMETHODCALLTYPE IsLost() override{return core_->IsLost();}
 HRESULT STDMETHODCALLTYPE Lock(LPRECT r,LPDDSURFACEDESC d,DWORD flags,HANDLE event) override{
  trace("LegacySurface.Lock");if(!d)return DDERR_INVALIDPARAMS;DDSURFACEDESC2 d2{};d2.dwSize=sizeof(d2);HRESULT hr=core_->Lock(r,&d2,flags,event);if(FAILED(hr))return hr;DWORD size=d->dwSize;std::memset(d,0,sizeof(*d));std::memcpy(d,&d2,std::min<size_t>(sizeof(*d),sizeof(d2)));d->dwSize=size?size:sizeof(*d);d->ddsCaps.dwCaps=d2.ddsCaps.dwCaps;return DD_OK;
 }
 HRESULT STDMETHODCALLTYPE ReleaseDC(HDC a) override{return core_->ReleaseDC(a);}
 HRESULT STDMETHODCALLTYPE Restore() override{return core_->Restore();}
 HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER a) override{return core_->SetClipper(a);}
 HRESULT STDMETHODCALLTYPE SetColorKey(DWORD a,LPDDCOLORKEY b) override{return core_->SetColorKey(a,b);}
 HRESULT STDMETHODCALLTYPE SetOverlayPosition(LONG a,LONG b) override{return core_->SetOverlayPosition(a,b);}
 HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE a) override{return core_->SetPalette(a);}
 HRESULT STDMETHODCALLTYPE Unlock(LPVOID) override{return core_->Unlock(nullptr);}
 HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT a,LPDIRECTDRAWSURFACE b,LPRECT c,DWORD d,LPDDOVERLAYFX e) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->UpdateOverlay(a,p?p->core():nullptr,c,d,e);}
 HRESULT STDMETHODCALLTYPE UpdateOverlayDisplay(DWORD a) override{return core_->UpdateOverlayDisplay(a);}
 HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD a,LPDIRECTDRAWSURFACE b) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->UpdateOverlayZOrder(a,p?p->core():nullptr);}
 // Surface2/3 retain the Surface1 layout but use versioned pointer types.
 HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE2 s) override{auto* p=dynamic_cast<LegacySurface*>(s);return p?core_->AddAttachedSurface(p->core()):DDERR_INVALIDOBJECT;}
 HRESULT STDMETHODCALLTYPE Blt(LPRECT a,LPDIRECTDRAWSURFACE2 b,LPRECT c,DWORD d,LPDDBLTFX e) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->Blt(a,p?p->core():nullptr,c,d,e);}
 HRESULT STDMETHODCALLTYPE BltFast(DWORD a,DWORD b,LPDIRECTDRAWSURFACE2 c,LPRECT d,DWORD e) override{auto* p=dynamic_cast<LegacySurface*>(c);return core_->BltFast(a,b,p?p->core():nullptr,d,e);}
 HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD a,LPDIRECTDRAWSURFACE2 b) override{auto* p=dynamic_cast<LegacySurface*>(b);return p?core_->DeleteAttachedSurface(a,p->core()):DDERR_INVALIDOBJECT;}
 HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE2 a,DWORD b) override{auto* p=dynamic_cast<LegacySurface*>(a);return core_->Flip(p?p->core():nullptr,b);}
 HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS caps,LPDIRECTDRAWSURFACE2* out) override{return getAttached(caps,out);}
 HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT a,LPDIRECTDRAWSURFACE2 b,LPRECT c,DWORD d,LPDDOVERLAYFX e) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->UpdateOverlay(a,p?p->core():nullptr,c,d,e);}
 HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD a,LPDIRECTDRAWSURFACE2 b) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->UpdateOverlayZOrder(a,p?p->core():nullptr);}
 HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE3 s) override{auto* p=dynamic_cast<LegacySurface*>(s);return p?core_->AddAttachedSurface(p->core()):DDERR_INVALIDOBJECT;}
 HRESULT STDMETHODCALLTYPE Blt(LPRECT a,LPDIRECTDRAWSURFACE3 b,LPRECT c,DWORD d,LPDDBLTFX e) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->Blt(a,p?p->core():nullptr,c,d,e);}
 HRESULT STDMETHODCALLTYPE BltFast(DWORD a,DWORD b,LPDIRECTDRAWSURFACE3 c,LPRECT d,DWORD e) override{auto* p=dynamic_cast<LegacySurface*>(c);return core_->BltFast(a,b,p?p->core():nullptr,d,e);}
 HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD a,LPDIRECTDRAWSURFACE3 b) override{auto* p=dynamic_cast<LegacySurface*>(b);return p?core_->DeleteAttachedSurface(a,p->core()):DDERR_INVALIDOBJECT;}
 HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE3 a,DWORD b) override{auto* p=dynamic_cast<LegacySurface*>(a);return core_->Flip(p?p->core():nullptr,b);}
 HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS caps,LPDIRECTDRAWSURFACE3* out) override{return getAttached(caps,out);}
 HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT a,LPDIRECTDRAWSURFACE3 b,LPRECT c,DWORD d,LPDDOVERLAYFX e) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->UpdateOverlay(a,p?p->core():nullptr,c,d,e);}
 HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD a,LPDIRECTDRAWSURFACE3 b) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->UpdateOverlayZOrder(a,p?p->core():nullptr);}
 HRESULT STDMETHODCALLTYPE GetDDInterface(LPVOID* out) override{return core_->GetDDInterface(out);}
 HRESULT STDMETHODCALLTYPE PageLock(DWORD a) override{return core_->PageLock(a);}
 HRESULT STDMETHODCALLTYPE PageUnlock(DWORD a) override{return core_->PageUnlock(a);}
 HRESULT STDMETHODCALLTYPE SetSurfaceDesc(LPDDSURFACEDESC d,DWORD flags) override{if(!d)return DDERR_INVALIDPARAMS;DDSURFACEDESC2 d2{};std::memcpy(&d2,d,std::min<size_t>(d->dwSize,sizeof(*d)));d2.dwSize=sizeof(d2);d2.ddsCaps.dwCaps=d->ddsCaps.dwCaps;return core_->SetSurfaceDesc(&d2,flags);}
 // Surface4 switches the surface, descriptor, caps and callback types to v2.
 HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE4 s) override{auto* p=dynamic_cast<LegacySurface*>(s);return p?core_->AddAttachedSurface(p->core()):DDERR_INVALIDOBJECT;}
 HRESULT STDMETHODCALLTYPE Blt(LPRECT a,LPDIRECTDRAWSURFACE4 b,LPRECT c,DWORD d,LPDDBLTFX e) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->Blt(a,p?p->core():nullptr,c,d,e);}
 HRESULT STDMETHODCALLTYPE BltFast(DWORD a,DWORD b,LPDIRECTDRAWSURFACE4 c,LPRECT d,DWORD e) override{auto* p=dynamic_cast<LegacySurface*>(c);return core_->BltFast(a,b,p?p->core():nullptr,d,e);}
 HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD a,LPDIRECTDRAWSURFACE4 b) override{auto* p=dynamic_cast<LegacySurface*>(b);return p?core_->DeleteAttachedSurface(a,p->core()):DDERR_INVALIDOBJECT;}
 HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID,LPDDENUMSURFACESCALLBACK2) override{return unsupported("LegacySurface4.EnumAttachedSurfaces");}
 HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD,LPVOID,LPDDENUMSURFACESCALLBACK2) override{return unsupported("LegacySurface4.EnumOverlayZOrders");}
 HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE4 a,DWORD b) override{auto* p=dynamic_cast<LegacySurface*>(a);return core_->Flip(p?p->core():nullptr,b);}
 HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS2 caps,LPDIRECTDRAWSURFACE4* out) override{
  trace("LegacySurface4.GetAttachedSurface");if(!out)return DDERR_INVALIDPARAMS;*out=nullptr;LPDIRECTDRAWSURFACE7 found{};HRESULT hr=core_->GetAttachedSurface(caps,&found);if(FAILED(hr))return hr;auto* s=dynamic_cast<Surface*>(found);if(!s){found->Release();return DDERR_INVALIDOBJECT;}auto* wrapped=new(std::nothrow)LegacySurface(s);if(!wrapped){found->Release();return DDERR_OUTOFMEMORY;}*out=static_cast<IDirectDrawSurface4*>(wrapped);return DD_OK;
 }
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS2 c) override{return core_->GetCaps(c);}
 HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC2 d) override{return core_->GetSurfaceDesc(d);}
 HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW,LPDDSURFACEDESC2) override{return DDERR_ALREADYINITIALIZED;}
 HRESULT STDMETHODCALLTYPE Lock(LPRECT r,LPDDSURFACEDESC2 d,DWORD flags,HANDLE event) override{return core_->Lock(r,d,flags,event);}
 HRESULT STDMETHODCALLTYPE Unlock(LPRECT r) override{return core_->Unlock(r);}
 HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT a,LPDIRECTDRAWSURFACE4 b,LPRECT c,DWORD d,LPDDOVERLAYFX e) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->UpdateOverlay(a,p?p->core():nullptr,c,d,e);}
 HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD a,LPDIRECTDRAWSURFACE4 b) override{auto* p=dynamic_cast<LegacySurface*>(b);return core_->UpdateOverlayZOrder(a,p?p->core():nullptr);}
 HRESULT STDMETHODCALLTYPE SetSurfaceDesc(LPDDSURFACEDESC2 d,DWORD flags) override{return core_->SetSurfaceDesc(d,flags);}
 HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a,LPVOID b,DWORD c,DWORD d) override{return core_->SetPrivateData(a,b,c,d);}
 HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a,LPVOID b,LPDWORD c) override{return core_->GetPrivateData(a,b,c);}
 HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID a) override{return core_->FreePrivateData(a);}
 HRESULT STDMETHODCALLTYPE GetUniquenessValue(LPDWORD a) override{return core_->GetUniquenessValue(a);}
 HRESULT STDMETHODCALLTYPE ChangeUniquenessValue() override{return core_->ChangeUniquenessValue();}
private:
 template<class T> HRESULT getAttached(LPDDSCAPS caps,T** out){
  trace("LegacySurface.GetAttachedSurface");if(!out)return DDERR_INVALIDPARAMS;*out=nullptr;DDSCAPS2 c{};if(caps)c.dwCaps=caps->dwCaps;LPDIRECTDRAWSURFACE7 found{};HRESULT hr=core_->GetAttachedSurface(caps?&c:nullptr,&found);if(FAILED(hr))return hr;auto* s=dynamic_cast<Surface*>(found);if(!s){found->Release();return DDERR_INVALIDOBJECT;}auto* wrapped=new(std::nothrow)LegacySurface(s);if(!wrapped){found->Release();return DDERR_OUTOFMEMORY;}*out=static_cast<T*>(wrapped);return DD_OK;
 }
 HRESULT unsupported(const char* n){trace(n);return DDERR_UNSUPPORTED;}
 std::atomic<ULONG> refs_{1};Surface* core_{};
};

// The shipped executable performs an old DirectDrawCreate -> IDirectDraw2
// availability check before it creates the real IDirectDraw7 renderer.  Keep
// that ABI as a small facade over the owned DD7 core so the check observes one
// coherent implementation without forwarding to system DirectDraw.
class LegacyDD final:public IDirectDraw,public IDirectDraw2 {
public:
 explicit LegacyDD(DD* core):core_(core){trace("LegacyDD.Create");}
 ~LegacyDD(){core_->Release();trace("LegacyDD.Destroy");}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r,void** out) override{
  trace_iid("LegacyDD.QueryInterface",r);if(!out)return E_POINTER;*out=nullptr;
  if(r==IID_IUnknown||r==IID_IDirectDraw)*out=static_cast<IDirectDraw*>(this);
  else if(r==IID_IDirectDraw2)*out=static_cast<IDirectDraw2*>(this);
  else return core_->QueryInterface(r,out);
  AddRef();return S_OK;
 }
 ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
 ULONG STDMETHODCALLTYPE Release() override{ULONG n=--refs_;if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Compact() override{return core_->Compact();}
 HRESULT STDMETHODCALLTYPE CreateClipper(DWORD a,LPDIRECTDRAWCLIPPER* b,IUnknown* c) override{return core_->CreateClipper(a,b,c);}
 HRESULT STDMETHODCALLTYPE CreatePalette(DWORD a,LPPALETTEENTRY b,LPDIRECTDRAWPALETTE* c,IUnknown* d) override{return core_->CreatePalette(a,b,c,d);}
 HRESULT STDMETHODCALLTYPE CreateSurface(LPDDSURFACEDESC d,LPDIRECTDRAWSURFACE* out,IUnknown* outer) override{
  trace("LegacyDD.CreateSurface");if(!d||!out||outer)return DDERR_INVALIDPARAMS;*out=nullptr;DDSURFACEDESC2 d2{};std::memcpy(&d2,d,std::min<size_t>(d->dwSize,sizeof(*d)));d2.dwSize=sizeof(d2);d2.ddsCaps.dwCaps=d->ddsCaps.dwCaps;LPDIRECTDRAWSURFACE7 made{};HRESULT hr=core_->CreateSurface(&d2,&made,nullptr);if(FAILED(hr))return hr;auto* s=dynamic_cast<Surface*>(made);if(!s){made->Release();return DDERR_INVALIDOBJECT;}auto* wrapped=new(std::nothrow)LegacySurface(s);if(!wrapped){made->Release();return DDERR_OUTOFMEMORY;}*out=wrapped;return DD_OK;
 }
 HRESULT STDMETHODCALLTYPE DuplicateSurface(LPDIRECTDRAWSURFACE,LPDIRECTDRAWSURFACE*) override{return unsupported("LegacyDD.DuplicateSurface");}
 HRESULT STDMETHODCALLTYPE EnumDisplayModes(DWORD,LPDDSURFACEDESC filter,LPVOID context,LPDDENUMMODESCALLBACK cb) override{
  trace("LegacyDD.EnumDisplayModes");if(!cb)return DDERR_INVALIDPARAMS;
  const SIZE modes[]={{800,600},{1024,768},{1280,720},{1920,1080},{3840,2160}};
  for(auto m:modes){DDSURFACEDESC d{};d.dwSize=sizeof(d);d.dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|DDSD_REFRESHRATE;d.dwWidth=m.cx;d.dwHeight=m.cy;d.dwRefreshRate=60;d.ddpfPixelFormat=bgra32();if(filter&&filter->dwWidth&&filter->dwWidth!=d.dwWidth)continue;if(cb(&d,context)==DDENUMRET_CANCEL)break;}return DD_OK;
 }
 HRESULT STDMETHODCALLTYPE EnumSurfaces(DWORD,LPDDSURFACEDESC,LPVOID,LPDDENUMSURFACESCALLBACK) override{return unsupported("LegacyDD.EnumSurfaces");}
 HRESULT STDMETHODCALLTYPE FlipToGDISurface() override{return core_->FlipToGDISurface();}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDCAPS a,LPDDCAPS b) override{return core_->GetCaps(a,b);}
 HRESULT STDMETHODCALLTYPE GetDisplayMode(LPDDSURFACEDESC d) override{
  trace("LegacyDD.GetDisplayMode");if(!d)return DDERR_INVALIDPARAMS;DWORD size=d->dwSize;std::memset(d,0,sizeof(*d));d->dwSize=size?size:sizeof(*d);d->dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|DDSD_REFRESHRATE;d->dwWidth=core_->width;d->dwHeight=core_->height;d->dwRefreshRate=60;d->ddpfPixelFormat=bgra32();return DD_OK;
 }
 HRESULT STDMETHODCALLTYPE GetFourCCCodes(LPDWORD a,LPDWORD b) override{return core_->GetFourCCCodes(a,b);}
 HRESULT STDMETHODCALLTYPE GetGDISurface(LPDIRECTDRAWSURFACE*) override{return unsupported("LegacyDD.GetGDISurface");}
 HRESULT STDMETHODCALLTYPE GetMonitorFrequency(LPDWORD a) override{return core_->GetMonitorFrequency(a);}
 HRESULT STDMETHODCALLTYPE GetScanLine(LPDWORD a) override{return core_->GetScanLine(a);}
 HRESULT STDMETHODCALLTYPE GetVerticalBlankStatus(LPBOOL a) override{return core_->GetVerticalBlankStatus(a);}
 HRESULT STDMETHODCALLTYPE Initialize(GUID* a) override{return core_->Initialize(a);}
 HRESULT STDMETHODCALLTYPE RestoreDisplayMode() override{return core_->RestoreDisplayMode();}
 HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND a,DWORD b) override{return core_->SetCooperativeLevel(a,b);}
 HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD a,DWORD b,DWORD c) override{return core_->SetDisplayMode(a,b,c,0,0);}
 HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD a,DWORD b,DWORD c,DWORD d,DWORD e) override{return core_->SetDisplayMode(a,b,c,d,e);}
 HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(DWORD a,HANDLE b) override{return core_->WaitForVerticalBlank(a,b);}
 HRESULT STDMETHODCALLTYPE GetAvailableVidMem(LPDDSCAPS,LPDWORD total,LPDWORD free) override{trace("LegacyDD.GetAvailableVidMem");if(total)*total=512u<<20;if(free)*free=384u<<20;return DD_OK;}
private:
 HRESULT unsupported(const char* n){trace(n);return DDERR_UNSUPPORTED;}
 std::atomic<ULONG> refs_{1};DD* core_{};
};

// ---- Scene thread (render_thread option): native sinks over a device copy ---
// The bridge's queued world commands (the retained scene seam, transparent
// terrain, wound layers) submit through these on the scene thread. They do
// what the Device sinks do, with the D3D7 fixed state and frame state copied
// when the command was queued (Device::captureSceneSinks), the stage-0 texture
// the bridge binds as a snapshot, and a wound surface resolved to its
// snapshot. The device's own bookkeeping (its pending legacy draws, UI flush,
// native sequence) ran on the game thread before the command was queued.
struct SceneSinks {
 Renderer* renderer=nullptr;
 FixedState state{};
 NativeFrameState frame{};bool frame_valid=false;
 std::shared_ptr<SurfaceImage> bound;
 std::uintptr_t wound_interface=0;std::shared_ptr<SurfaceImage> wound;
 bool sequence=false,batch_active=false,object_active=false;
 Renderer::NativeQueueCheckpoint checkpoint{};
 void flushUi(){if(renderer->has_pending_ui())(void)renderer->flush_ui();}
 SurfaceImage* woundImage(std::uintptr_t surface_interface) const{return surface_interface&&surface_interface==wound_interface?wound.get():nullptr;}
 static SceneSinks& of(void* context){return *static_cast<SceneSinks*>(context);}
 static bool submit(void* context,const NativeGeometryView& view,NativeGeometryResult& result) noexcept{
  try{
   auto& self=of(context);auto& renderer=*self.renderer;result={};
   if(!self.sequence){self.flushUi();self.sequence=true;}
   if(view.static_submission){
    const auto& request=*view.static_submission;
    using Operation=NativeStaticSubmission::Operation;
    request.generation=renderer.native_static_generation();
    if(request.operation==Operation::generation)return true;
    if(request.operation==Operation::shadow_static||request.operation==Operation::shadow_dynamic){
     request.generation=renderer.set_native_shadow_phase(request.operation==Operation::shadow_static?1u:2u);return true;}
    if(request.operation==Operation::terrain_frame){
     if(!request.terrain_state){result.failure_hresult=E_INVALIDARG;result.failure_operation="scene.terrain_frame";return false;}
     return SUCCEEDED(renderer.set_native_terrain_frame(*request.terrain_state,request.full_sun,request.layer_shifts));
    }
    if(request.operation==Operation::remove){renderer.remove_native_static_object(request.key,request.terrain);return true;}
    if(request.operation==Operation::flush){
     if(!request.world_to_clip){result.failure_hresult=E_INVALIDARG;result.failure_operation="scene.static_clip";return false;}
     const HRESULT hr=request.terrain?renderer.flush_native_static_terrain(self.state,*request.world_to_clip):
      renderer.flush_native_static(self.state,*request.world_to_clip);
     if(FAILED(hr)){result.failure_hresult=hr;result.failure_operation="scene.static_flush";
      char detail[96];std::snprintf(detail,sizeof(detail),"hr=%08lx draws=%zu",static_cast<unsigned long>(hr),renderer.native_static_draw_count());
      owned::bridge_append_probe_event("static_flush_failed",detail);return false;}
     return true;
    }
    bool registered=false;HRESULT hr=S_OK;
    if(request.terrain){
     if(!request.terrain_view){result.failure_hresult=E_INVALIDARG;result.failure_operation="scene.static_terrain_view";return false;}
     const auto& terrain=*request.terrain_view;
     const FixedState terrain_state=terrain.owned_fixed_state.enabled?native_owned_submission_state(self.state,terrain.owned_fixed_state):self.state;
     hr=renderer.set_native_static_terrain(request.key,terrain,terrain_state,
      terrain.owned_texture.identity?nullptr:self.bound.get(),registered,&result);
     request.registered=registered;return SUCCEEDED(hr);
    }
    if(!request.records){result.failure_hresult=E_INVALIDARG;result.failure_operation="scene.static_records";return false;}
    hr=renderer.set_native_static_object(request.key,*request.records,self.state,registered,&result);
    request.registered=registered;
    if(FAILED(hr)){char detail[160];std::snprintf(detail,sizeof(detail),"hr=%08lx op=%s",static_cast<unsigned long>(hr),
     result.failure_operation?result.failure_operation:"-");owned::bridge_append_probe_event("static_set_failed",detail);}
    return SUCCEEDED(hr);
   }
   if(view.depth_submission)return SUCCEEDED(renderer.queue_native_depth(*view.depth_submission,self.state,&result));
   if(view.prepared_submission){
    const auto& submission=*view.prepared_submission;SurfaceImage* wound=nullptr;
    if(submission.wound_surface_interface){
     wound=self.woundImage(submission.wound_surface_interface);
     if(!wound){result.failure_hresult=E_INVALIDARG;result.failure_operation="scene.prepared_wound";return false;}
    }
    return SUCCEEDED(renderer.queue_native_records(submission,self.state,wound,&result));
   }
   const bool prepared=view.prepared_mesh&&view.prepared_material&&view.prepared_identity.valid;
   const FixedState submission_state=view.owned_fixed_state.enabled?native_owned_submission_state(self.state,view.owned_fixed_state):self.state;
   const auto queue=[&](const NativeGeometryView& packet,const FixedState& state,SurfaceImage* texture,NativeGeometryResult* output){
    if(packet.owned_texture.identity)return renderer.queue_native_owned(packet,state,output);
    return prepared?renderer.queue_native_prepared(packet,state,texture,output):renderer.queue_native(packet,state,texture,output);
   };
   SurfaceImage* wound=view.wound_surface_interface?self.woundImage(view.wound_surface_interface):nullptr;
   if(view.wound_only&&!wound)return false;
   const bool owned_depth=view.owned_fixed_state.enabled&&view.contribution.known&&!view.contribution.color&&view.contribution.light_mask;
   if(!wound)return SUCCEEDED(queue(view,submission_state,view.owned_texture.identity||owned_depth?nullptr:self.bound.get(),&result));
   if(!view.wound_only&&FAILED(queue(view,submission_state,self.bound.get(),&result)))return false;
   FixedState wound_state=submission_state;
   wound_state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;wound_state.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_INVSRCALPHA;
   wound_state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=TRUE;wound_state.stage[0][D3DTSS_TEXCOORDINDEX]=0;
   NativeGeometryView wound_view=view;wound_view.wound_surface_interface=0;wound_view.wound_only=false;wound_view.owned_texture={};
   if(prepared)wound_view.prepared_material=view.prepared_wound_material;
   if(prepared&&!wound_view.prepared_material)return false;
   if(FAILED(prepared?renderer.queue_native_prepared(wound_view,wound_state,wound,view.wound_only?&result:nullptr):
                      renderer.queue_native(wound_view,wound_state,wound,view.wound_only?&result:nullptr)))return !view.wound_only;
   return true;
  }catch(...){if(!result.failure_operation)result.failure_operation="scene.exception";return false;}
 }
 static bool beginBatch(void* context,SemanticKind) noexcept{
  try{auto& self=of(context);if(self.batch_active)return false;self.flushUi();
   if(FAILED(self.renderer->flush_native()))return false;self.sequence=true;self.batch_active=true;return true;}catch(...){return false;}
 }
 static void endBatch(void* context,bool commit) noexcept{
  try{auto& self=of(context);if(!self.batch_active)return;if(!commit)self.renderer->discard_native();self.batch_active=false;}catch(...){}
 }
 static bool beginObject(void* context) noexcept{
  try{auto& self=of(context);if(self.object_active||!self.renderer->checkpoint_native(self.checkpoint))return false;self.object_active=true;return true;}catch(...){return false;}
 }
 static void endObject(void* context,bool commit) noexcept{
  try{auto& self=of(context);if(!self.object_active)return;if(!commit)self.renderer->rollback_native(self.checkpoint);else self.renderer->release_native_checkpoint(self.checkpoint);self.object_active=false;}catch(...){}
 }
 static bool frameState(void* context,NativeFrameState& output) noexcept{
  const auto& self=of(context);if(!self.frame_valid)return false;output=self.frame;return true;
 }
 static bool worldLighting(void* context,const NativeWorldLighting& lighting) noexcept{
  try{return of(context).renderer->set_native_world_lighting(lighting);}catch(...){return false;}
 }
 static bool pointLights(void* context,const RetainedVisualFrame& frame) noexcept{
  try{return of(context).renderer->set_native_point_lights(frame.world_point_lights,frame.time);}catch(...){return false;}
 }
 static bool pointShadowBatch(void* context,std::uint64_t& generation,bool& pressure) noexcept{
  try{auto& self=of(context);if(self.batch_active)return false;self.flushUi();self.sequence=true;
   return self.renderer->begin_native_point_shadow_batch(generation,&pressure);}catch(...){return false;}
 }
 static bool pointShadowAllocate(void* context,bool overlay,int& slot) noexcept{return of(context).renderer->allocate_native_point_shadow(overlay,slot);}
 static void pointShadowRelease(void* context,std::uint64_t generation,int slot) noexcept{of(context).renderer->release_native_point_shadow(generation,slot);}
 static void pointShadowPublish(void* context,const std::vector<NativePointShadowPublication>& lights,const NativePointShadowStats& stats) noexcept{
  try{of(context).renderer->publish_native_point_shadows(lights,stats);}catch(...){}
 }
 static bool pointShadowFaces(void* context,std::uint32_t light,std::uint32_t first_face,const NativePointShadowFrame& faces) noexcept{
  try{auto& self=of(context);if(self.batch_active)return false;self.sequence=true;
   return self.renderer->begin_native_point_shadow_faces(light,first_face,faces);}catch(...){return false;}
 }
 static bool pointShadowFacesEnd(void* context,bool commit) noexcept{try{return of(context).renderer->end_native_point_shadow_faces(commit);}catch(...){return false;}}
 static void cullProjection(void* context,NativeShadowCullProjection& projection) noexcept{of(context).renderer->native_shadow_cull_projection(projection);}
 static bool shadowsBegin(void* context,const NativeShadowFrame& frame,std::uint8_t& dirty) noexcept{
  try{auto& self=of(context);if(self.batch_active)return false;self.flushUi();self.sequence=true;
   return self.renderer->begin_native_shadows(frame,dirty);}catch(...){return false;}
 }
 static bool shadowsEnd(void* context,bool commit) noexcept{try{return of(context).renderer->end_native_shadows(commit);}catch(...){return false;}}
 static bool staticDepth(void* context,const RetainedShadowCasterValue* casters,std::uint32_t count,std::uint64_t store_revision,
  std::vector<RetainedShadowCasterValue>& missing) noexcept{
  try{return of(context).renderer->queue_native_static_shadow_depth(casters,count,store_revision,missing);}catch(...){return false;}
 }
 static bool terrainBake(void* context,const NativeTerrainShadowEntry* entries,std::uint32_t count,std::vector<std::uint32_t>& absent) noexcept{
  try{return of(context).renderer->queue_native_terrain_shadow_bake(entries,count,absent);}catch(...){return false;}
 }
 static void shadowDiagnostics(void* context,std::uint64_t world_serial,const NativeShadowSourceDiagnostics& values) noexcept{
  try{of(context).renderer->record_native_shadow_source_diagnostics(world_serial,values);}catch(...){}
 }
 static bool rainBegin(void* context,const NativeShadowSlice& slice,std::uint32_t side) noexcept{
  try{auto& self=of(context);if(self.batch_active)return false;self.sequence=true;
   return self.renderer->begin_native_rain_occlusion(slice,side);}catch(...){return false;}
 }
 static bool rainEnd(void* context,bool commit) noexcept{try{return of(context).renderer->end_native_rain_occlusion(commit);}catch(...){return false;}}
 static void install(void* context) noexcept{
  // The shelter (rain occlusion) sinks are thread-local like the others: the
  // scene thread's world preparation bakes through these.
  if(!context){bridge_set_native_geometry_sink(nullptr,nullptr);bridge_set_rain_occlusion_sink(nullptr,nullptr);return;}
  bridge_set_rain_occlusion_sink(&rainBegin,&rainEnd);
  bridge_set_native_geometry_sink(context,&submit,&beginBatch,&endBatch,&beginObject,&endObject,&frameState,&worldLighting,
   &shadowsBegin,&shadowsEnd,&shadowDiagnostics,&pointLights,&pointShadowFaces,&pointShadowFacesEnd,&staticDepth,
   &pointShadowBatch,&pointShadowPublish,&pointShadowAllocate,&pointShadowRelease,&cullProjection,&terrainBake);
 }
 static void bind(void* context,const std::shared_ptr<SurfaceImage>& texture) noexcept{of(context).bound=texture;}
 static void adopt(void* context,const void* from) noexcept{
  auto& self=of(context);const auto& source=*static_cast<const SceneSinks*>(from);
  self.state=source.state;self.frame=source.frame;self.frame_valid=source.frame_valid;self.bound=source.bound;
 }
 static void setWound(void* context,std::uintptr_t surface_interface,const std::shared_ptr<SurfaceImage>& texture) noexcept{
  auto& self=of(context);self.wound_interface=surface_interface;self.wound=texture;
 }
};

class Device final:public IDirect3DDevice7 {
public:
#ifdef EID3D11_TESTING
 friend bool test_visual_gpu_surface_contract() noexcept;
 friend bool test_shipped_visual_gpu(const ResourceStoreConfig&,const ResourceStoreConfig&);
 friend void test_terrain_shadow_material_contract(DD&,Device&);
#endif
 Device(DD* dd,Surface* target):dd_(dd),target_(target),composition_target_(target){
  dd_->AddRef();if(target_)target_->AddRef();if(composition_target_)composition_target_->AddRef();
  dd_->active_target=target_;dd_->active_device=this;
  // Windowed native DirectDraw creates its render surface from the game's
  // video rectangle, independently of our fitted physical client window.
  // Publish that authoritative canvas before CInterface3D initializes.
  if(target_){
   dd_->width=target_->image()->width;dd_->height=target_->image()->height;
   owned::widescreen_set_surface(dd_->width,dd_->height);
  }
  state_.viewport.dwWidth=dd_->width;state_.viewport.dwHeight=dd_->height;state_.viewport.dvMaxZ=1;
  RECT client{};const bool sized=GetClientRect(dd_->hwnd,&client)&&client.right>0&&client.bottom>0;
  dd_->renderer.ensure(dd_->hwnd,sized?UINT(client.right):dd_->width,sized?UINT(client.bottom):dd_->height);
  bridge_set_engine_frame_begin_sink(this,&Device::beginEngineFrame);
  trace("Device.Create");
 }
 ~Device(){bridge_clear_engine_frame_begin_sink(this);bridge_clear_scene_handoff(this);if(scene_){bridge_set_native_geometry_sink(nullptr,nullptr);bridge_set_screen_rectangle_sink(nullptr,nullptr);bridge_set_game_particle_sink(nullptr,nullptr);bridge_set_game_ribbon_sink(nullptr,nullptr);bridge_set_game_selection_sink(nullptr,nullptr);bridge_set_game_print_sink(nullptr,nullptr,nullptr);bridge_set_game_glyph_sink(nullptr,nullptr);camp_inventory_set_popup_sink(nullptr,nullptr);}if(native_object_active_){dd_->renderer.rollback_native(native_object_checkpoint_);native_object_active_=false;}flushNativeVisual();clearPendingDraws();releaseRecordingTextures();for(auto& pair:blocks_)releaseBlock(pair.second);for(auto* t:textures_)if(t)t->Release();if(dd_->active_device==this)dd_->active_device=nullptr;if(dd_->active_target==target_)dd_->active_target=nullptr;if(target_)target_->Release();if(composition_target_)composition_target_->Release();dd_->Release();trace("Device.Destroy");}
 bool compositionTarget(const Surface* surface)const noexcept{return surface==composition_target_;}
 bool displayResizeIdle() const noexcept {return !scene_ && !recording_ && !native_batch_active_ && !native_object_active_ && pending_draws_.empty();}
 void resizeDisplayViewport(UINT old_width,UINT old_height,UINT width,UINT height) noexcept {
  const auto update=[&](D3DVIEWPORT7& viewport){if(!viewport.dwX && !viewport.dwY && viewport.dwWidth==old_width && viewport.dwHeight==old_height){viewport.dwWidth=width;viewport.dwHeight=height;}};
  update(state_.viewport);for(auto& entry:blocks_)update(entry.second.state.viewport);
 }
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r,void** o) override{trace("Device.QueryInterface");if(!o)return E_POINTER;*o=nullptr;if(r==IID_IUnknown||r==IID_IDirect3DDevice7){*o=this;AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}ULONG STDMETHODCALLTYPE Release() override{ULONG n=--refs_;if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE GetCaps(LPD3DDEVICEDESC7) override;
 HRESULT STDMETHODCALLTYPE EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK,LPVOID) override;
 HRESULT STDMETHODCALLTYPE BeginScene() override{bridge_profile_api_entry("BeginScene",_ReturnAddress());trace("Device.BeginScene");if(scene_)return D3DERR_SCENE_IN_SCENE;clearPendingDraws();HRESULT pending=flushNativeVisual();if(FAILED(pending))return dd_->checkDevice(pending);scene_=true;native_sequence_=false;native_batch_active_=false;native_object_active_=false;bridge_profile_begin_frame();dd_->renderer.begin_scene();bridge_set_native_geometry_sink(this,&Device::submitNativeGeometry,&Device::beginNativeGeometryBatch,&Device::endNativeGeometryBatch,&Device::beginNativeObject,&Device::endNativeObject,&Device::nativeFrameState,&Device::nativeWorldLighting,&Device::beginNativeShadows,&Device::endNativeShadows,&Device::shadowSourceDiagnostics,&Device::nativePointLights,&Device::beginNativePointShadowFaces,&Device::endNativePointShadowFaces,&Device::queueNativeStaticDepth,&Device::beginNativePointShadowBatch,&Device::publishNativePointShadows,&Device::allocateNativePointShadow,&Device::releaseNativePointShadow,&Device::nativeShadowCullProjection,&Device::queueNativeTerrainBake);bridge_set_rain_occlusion_sink(&Device::beginRainOcclusion,&Device::endRainOcclusion);bridge_set_screen_rectangle_sink(this,&Device::submitScreenRectangle);bridge_set_game_particle_sink(this,&Device::submitGameParticles);bridge_set_game_ribbon_sink(this,&Device::submitGameRibbon);bridge_set_game_selection_sink(this,&Device::submitGameSelection);bridge_set_game_print_sink(this,&Device::submitGamePrint,&Device::flushGamePrintsSink);bridge_set_game_glyph_sink(this,&Device::submitGameGlyphs);camp_inventory_set_popup_sink(this,&Device::submitCampPopup,dd_->renderer.resource_device());registerSceneHandoff();return D3D_OK;}
 HRESULT STDMETHODCALLTYPE EndScene() override{bridge_profile_api_entry("EndScene",_ReturnAddress());trace("Device.EndScene");if(!scene_)return D3DERR_SCENE_NOT_IN_SCENE;flushGamePrints();bridge_clear_scene_handoff(this);bridge_set_native_geometry_sink(nullptr,nullptr);bridge_set_screen_rectangle_sink(nullptr,nullptr);bridge_set_game_particle_sink(nullptr,nullptr);bridge_set_game_ribbon_sink(nullptr,nullptr);bridge_set_game_selection_sink(nullptr,nullptr);bridge_set_game_print_sink(nullptr,nullptr,nullptr);bridge_set_game_glyph_sink(nullptr,nullptr);camp_inventory_set_popup_sink(nullptr,nullptr);native_batch_active_=false;if(native_object_active_){dd_->renderer.rollback_native(native_object_checkpoint_);native_object_active_=false;}HRESULT hr=flushNativeVisual();native_sequence_=false;if(SUCCEEDED(hr)){if(!pending_draws_.empty())invalidateVisualTarget();hr=dd_->renderer.draw_frame_take(pending_draws_,pending_vertex_bytes_,pending_indices_);}clearPendingDraws();dd_->renderer.end_scene();scene_=false;bridge_profile_end_frame();return dd_->checkDevice(hr);}
 HRESULT STDMETHODCALLTYPE GetDirect3D(LPDIRECT3D7* out) override{if(!out)return E_POINTER;*out=static_cast<IDirect3D7*>(dd_);dd_->AddRef();return D3D_OK;}
 HRESULT STDMETHODCALLTYPE SetRenderTarget(LPDIRECTDRAWSURFACE7,DWORD) override;
 HRESULT STDMETHODCALLTYPE GetRenderTarget(LPDIRECTDRAWSURFACE7*) override;
 HRESULT STDMETHODCALLTYPE Clear(DWORD,LPD3DRECT,DWORD,D3DCOLOR,D3DVALUE,DWORD) override;
 HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE,LPD3DMATRIX) override;
 HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE,LPD3DMATRIX) override;
 HRESULT STDMETHODCALLTYPE SetViewport(LPD3DVIEWPORT7) override;
 HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE,LPD3DMATRIX) override{return unsupported("Device.MultiplyTransform");}
 HRESULT STDMETHODCALLTYPE GetViewport(LPD3DVIEWPORT7) override;
 HRESULT STDMETHODCALLTYPE SetMaterial(LPD3DMATERIAL7) override;
 HRESULT STDMETHODCALLTYPE GetMaterial(LPD3DMATERIAL7) override;
 HRESULT STDMETHODCALLTYPE SetLight(DWORD,LPD3DLIGHT7) override{return unsupported("Device.SetLight");}
 HRESULT STDMETHODCALLTYPE GetLight(DWORD,LPD3DLIGHT7) override{return unsupported("Device.GetLight");}
 HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE,DWORD) override;
 HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE,LPDWORD) override;
 HRESULT STDMETHODCALLTYPE BeginStateBlock() override;
 HRESULT STDMETHODCALLTYPE EndStateBlock(LPDWORD) override;
 HRESULT STDMETHODCALLTYPE PreLoad(LPDIRECTDRAWSURFACE7) override{trace("Device.PreLoad");return D3D_OK;}
 HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE,DWORD,LPVOID,DWORD,DWORD) override;
 HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE,DWORD,LPVOID,DWORD,LPWORD,DWORD,DWORD) override;
 HRESULT STDMETHODCALLTYPE SetClipStatus(LPD3DCLIPSTATUS) override{return unsupported("Device.SetClipStatus");}
 HRESULT STDMETHODCALLTYPE GetClipStatus(LPD3DCLIPSTATUS) override{return unsupported("Device.GetClipStatus");}
 HRESULT STDMETHODCALLTYPE DrawPrimitiveStrided(D3DPRIMITIVETYPE,DWORD,LPD3DDRAWPRIMITIVESTRIDEDDATA,DWORD,DWORD) override{return unsupported("Device.DrawPrimitiveStrided");}
 HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveStrided(D3DPRIMITIVETYPE,DWORD,LPD3DDRAWPRIMITIVESTRIDEDDATA,DWORD,LPWORD,DWORD,DWORD) override{return unsupported("Device.DrawIndexedPrimitiveStrided");}
 HRESULT STDMETHODCALLTYPE DrawPrimitiveVB(D3DPRIMITIVETYPE,LPDIRECT3DVERTEXBUFFER7,DWORD,DWORD,DWORD) override{return unsupported("Device.DrawPrimitiveVB");}
 HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveVB(D3DPRIMITIVETYPE,LPDIRECT3DVERTEXBUFFER7,DWORD,DWORD,LPWORD,DWORD,DWORD) override{return unsupported("Device.DrawIndexedPrimitiveVB");}
 HRESULT STDMETHODCALLTYPE ComputeSphereVisibility(LPD3DVECTOR,LPD3DVALUE,DWORD,DWORD,LPDWORD) override{return unsupported("Device.ComputeSphereVisibility");}
 HRESULT STDMETHODCALLTYPE GetTexture(DWORD,LPDIRECTDRAWSURFACE7*) override;
 HRESULT STDMETHODCALLTYPE SetTexture(DWORD,LPDIRECTDRAWSURFACE7) override;
 HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD,D3DTEXTURESTAGESTATETYPE,LPDWORD) override;
 HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD,D3DTEXTURESTAGESTATETYPE,DWORD) override;
 HRESULT STDMETHODCALLTYPE ValidateDevice(LPDWORD passes) override{if(passes)*passes=1;return D3D_OK;}
 HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD) override;
 HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD) override;
 HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD) override;
 HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE,LPDWORD) override;
 HRESULT STDMETHODCALLTYPE Load(LPDIRECTDRAWSURFACE7,LPPOINT,LPDIRECTDRAWSURFACE7,LPRECT,DWORD) override;
 HRESULT STDMETHODCALLTYPE LightEnable(DWORD,BOOL) override{return unsupported("Device.LightEnable");}
 HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD,BOOL*) override{return unsupported("Device.GetLightEnable");}
 HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD,D3DVALUE*) override{return unsupported("Device.SetClipPlane");}
 HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD,D3DVALUE*) override{return unsupported("Device.GetClipPlane");}
 HRESULT STDMETHODCALLTYPE GetInfo(DWORD,LPVOID,DWORD) override{return unsupported("Device.GetInfo");}
 HRESULT flushForSurface(){PostEngineSurfaceTimingScope timing(PostEngineSurfaceTimingKind::flush);HRESULT hr=flushPendingDraws();
  if(FAILED(hr)){reportSurfaceFlushFailure("legacy",hr);return hr;}
  hr=flushNativeVisual();native_sequence_=false;if(FAILED(hr))reportSurfaceFlushFailure("native",hr);return hr;}
 void reportSurfaceFlushFailure(const char* stage,HRESULT hr){static unsigned reported=0;if(reported>=8)return;++reported;
  char detail[128];std::snprintf(detail,sizeof(detail),"stage=%s hr=%08lx line=%d static_draws=%zu",stage,static_cast<unsigned long>(hr),
   dd_->renderer.native_flush_failure_line(),dd_->renderer.native_static_draw_count());owned::bridge_append_probe_event("surface_flush_failed",detail);}
 bool visualWritesPending() const noexcept{return native_sequence_||!pending_draws_.empty();}
private:
 static bool beginEngineFrame(void* context) noexcept {
  auto& self=*static_cast<Device*>(context);
  if(self.dd_->active_device!=&self||self.scene_)return false;
  try {
   HRESULT hr=self.flushForSurface();
   if(SUCCEEDED(hr)){
    self.dd_->invalidateVisualOutput();
    hr=self.dd_->renderer.begin_engine_frame();
   }
   return SUCCEEDED(hr);
  }catch(...){return false;}
 }
 void invalidateVisualTarget() noexcept{if(target_)target_->invalidateVisualTarget();}
 HRESULT flushNativeVisual(){if(native_sequence_)invalidateVisualTarget();else if(dd_->renderer.scene_queue()&&!dd_->renderer.scene_native_pending())return D3D_OK;return dd_->renderer.flush_native();}
 struct StateBlock {FixedState state{};std::array<Surface*,8> textures{};std::array<bool,256> render_mask{};std::array<std::array<bool,32>,8> stage_mask{};std::array<bool,8> texture_mask{};std::array<bool,4> transform_mask{};bool viewport_mask{},material_mask{};D3DSTATEBLOCKTYPE type{D3DSBT_ALL};};
 void releaseBlock(StateBlock& b){for(auto*& t:b.textures){if(t)t->Release();t=nullptr;}}
 void captureBlock(StateBlock& b){for(size_t i=0;i<b.render_mask.size();++i)if(b.render_mask[i])b.state.render[i]=state_.render[i];for(size_t s=0;s<b.stage_mask.size();++s)for(size_t i=0;i<b.stage_mask[s].size();++i)if(b.stage_mask[s][i])b.state.stage[s][i]=state_.stage[s][i];for(size_t i=0;i<b.transform_mask.size();++i)if(b.transform_mask[i])b.state.transform[i]=state_.transform[i];if(b.viewport_mask)b.state.viewport=state_.viewport;if(b.material_mask)b.state.material=state_.material;for(size_t i=0;i<b.texture_mask.size();++i)if(b.texture_mask[i]){if(b.textures[i])b.textures[i]->Release();b.textures[i]=textures_[i];if(b.textures[i])b.textures[i]->AddRef();}}
 void applyBlock(DWORD handle,const StateBlock& b){for(size_t i=0;i<b.render_mask.size();++i)if(b.render_mask[i])state_.render[i]=b.state.render[i];for(size_t s=0;s<b.stage_mask.size();++s)for(size_t i=0;i<b.stage_mask[s].size();++i)if(b.stage_mask[s][i])state_.stage[s][i]=b.state.stage[s][i];for(size_t i=0;i<b.transform_mask.size();++i)if(b.transform_mask[i])state_.transform[i]=b.state.transform[i];if(b.viewport_mask)state_.viewport=b.state.viewport;if(b.material_mask)state_.material=b.state.material;for(size_t i=0;i<b.texture_mask.size();++i)if(b.texture_mask[i]){auto* next=b.textures[i];if(next)next->AddRef();if(textures_[i])textures_[i]->Release();textures_[i]=next;}if(state_trace_enabled()){char event[320]{};sprintf_s(event,"Diagnostic.StateBlockApply.h%lu.afm%d.aem%d.arm%d.af%lu.ae%lu.ar%lu.bm%d.b%lu.sm%d.s%lu.dm%d.d%lu.tm%d",handle,b.render_mask[D3DRENDERSTATE_ALPHAFUNC]?1:0,b.render_mask[D3DRENDERSTATE_ALPHATESTENABLE]?1:0,b.render_mask[D3DRENDERSTATE_ALPHAREF]?1:0,state_.render[D3DRENDERSTATE_ALPHAFUNC],state_.render[D3DRENDERSTATE_ALPHATESTENABLE],state_.render[D3DRENDERSTATE_ALPHAREF],b.render_mask[D3DRENDERSTATE_ALPHABLENDENABLE]?1:0,state_.render[D3DRENDERSTATE_ALPHABLENDENABLE],b.render_mask[D3DRENDERSTATE_SRCBLEND]?1:0,state_.render[D3DRENDERSTATE_SRCBLEND],b.render_mask[D3DRENDERSTATE_DESTBLEND]?1:0,state_.render[D3DRENDERSTATE_DESTBLEND],b.texture_mask[0]?1:0);trace(event);}}
  void releaseRecordingTextures(){for(auto*& texture:recording_textures_){if(texture)texture->Release();texture=nullptr;}}
 // Owned DrawFrameSelection: keep submission order with the queued legacy and
 // interface work, then hand the request to the renderer.
 // Scene thread hand-off (bridge_set_scene_handoff): queued world commands
 // and the device-state copy their scene sinks read.
 void registerSceneHandoff() noexcept{
  BridgeSceneHandoff handoff{};handoff.context=this;
  // Camp inventory composition keeps the device path (its icon textures are
  // resolved against the live bound surface). The world draws into the
  // composition output too, so only the composing state can tell them apart.
  handoff.active=[](void* context){
   return !camp::composing&&static_cast<Device*>(context)->dd_->renderer.scene_queue();};
  handoff.post=[](void* context,std::function<void()> work){
   auto& renderer=static_cast<Device*>(context)->dd_->renderer;
   renderer.scene_note_native();renderer.scene_post_frame(std::move(work));return true;};
  handoff.capture_sinks=&Device::captureSceneSinks;
  handoff.install_sinks=&SceneSinks::install;
  handoff.bound_texture=[](void* context){auto* self=static_cast<Device*>(context);
   return self->dd_->renderer.scene_surface(self->textures_[0]?self->textures_[0]->image():nullptr);};
  handoff.bind_texture=&SceneSinks::bind;
  handoff.surface_snapshot=[](void* context,std::uintptr_t surface_interface){auto* self=static_cast<Device*>(context);
   auto* surface=surface_interface?dynamic_cast<Surface*>(reinterpret_cast<IDirectDrawSurface7*>(surface_interface)):nullptr;
   return self->dd_->renderer.scene_surface(surface?surface->image():nullptr);};
  handoff.set_wound=&SceneSinks::setWound;
  handoff.sync=[](void* context,const char* reason){static_cast<Device*>(context)->dd_->renderer.scene_sync(reason);};
  handoff.adopt_sinks=&SceneSinks::adopt;
  bridge_set_scene_handoff(handoff);
 }
 // Game thread: a native sequence begins (queued legacy draws and UI go
 // first), then the state the scene sinks read is copied.
 static std::shared_ptr<void> captureSceneSinks(void* context){
  auto* self=static_cast<Device*>(context);
  try{
   if(FAILED(self->flushPendingDraws())||FAILED(self->dd_->flushVisualUi()))return nullptr;
   self->native_sequence_=true;self->invalidateVisualTarget();self->dd_->renderer.scene_note_native();
   auto sinks=std::make_shared<SceneSinks>();
   sinks->renderer=&self->dd_->renderer;sinks->state=self->state_;
   sinks->frame_valid=nativeFrameState(self,sinks->frame);
   return sinks;
  }catch(...){return nullptr;}
 }
 static bool submitScreenRectangle(void* context,const ScreenRectangleRequest& request) noexcept{auto* self=static_cast<Device*>(context);if(!self->scene_)return false;HRESULT hr=self->flushNativeVisual();self->native_sequence_=false;if(SUCCEEDED(hr))hr=self->dd_->flushVisualUi();if(SUCCEEDED(hr))hr=self->flushPendingDraws();if(SUCCEEDED(hr)){self->invalidateVisualTarget();hr=self->dd_->renderer.queue_screen_rectangle(request,self->state_.viewport);}return SUCCEEDED(hr);}
 static bool submitNativeGeometry(void* context,const NativeGeometryView& view,NativeGeometryResult& result) noexcept{try{return static_cast<Device*>(context)->queueNativeGeometry(view,result);}catch(...){if(!result.failure_operation)result.failure_operation="proxy.exception";return false;}}
 static bool submitGameParticles(void* context,const GameParticleView& view) noexcept{try{return static_cast<Device*>(context)->drawGameParticles(view);}catch(...){return false;}}
 bool drawGameParticles(const GameParticleView& view){
  if(!scene_||native_batch_active_)return false;
  if(FAILED(flushPendingDraws())||FAILED(dd_->flushVisualUi())||FAILED(flushNativeVisual()))return false;
  native_sequence_=false;invalidateVisualTarget();
  FixedState state=state_;
  if(view.additive){state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;state.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_ONE;}
  return SUCCEEDED(dd_->renderer.draw_game_particles(view,state,textures_[0]?textures_[0]->image():nullptr));
 }
 static bool submitGameRibbon(void* context,const GameRibbonView& view) noexcept{try{return static_cast<Device*>(context)->drawGameRibbon(view);}catch(...){return false;}}
 // Stock Draw3DLine sets SRCALPHA/ONE around its one draw and restores both.
 bool drawGameRibbon(const GameRibbonView& view){
  if(!scene_||native_batch_active_)return false;
  if(FAILED(flushPendingDraws())||FAILED(dd_->flushVisualUi())||FAILED(flushNativeVisual()))return false;
  native_sequence_=false;invalidateVisualTarget();
  FixedState state=state_;
  state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;state.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_ONE;
  return SUCCEEDED(dd_->renderer.draw_game_ribbon(view,state,textures_[0]?textures_[0]->image():nullptr));
 }
 static bool submitGameSelection(void* context,const GameSelectionView& view) noexcept{try{return static_cast<Device*>(context)->drawGameSelection(view);}catch(...){return false;}}
 // The marker draws with the state Draw3DScene set (stock Render keeps it).
 bool drawGameSelection(const GameSelectionView& view){
  if(!scene_||native_batch_active_)return false;
  if(FAILED(flushPendingDraws())||FAILED(dd_->flushVisualUi())||FAILED(flushNativeVisual()))return false;
  native_sequence_=false;invalidateVisualTarget();
  return SUCCEEDED(dd_->renderer.draw_game_selection(view,state_,textures_[0]?textures_[0]->image():nullptr));
 }
 // Terrain prints: one list's prints are batched per texture/state run and
 // drawn when the bridge reports the list's last print (or at EndScene).
 struct GamePrintRun{SurfaceImage* texture=nullptr;FixedState state{};std::array<float,16> object_to_clip{};float half_width=0,half_height=0;UINT first=0,count=0;};
 std::vector<GameParticleGpu> print_vertices_;std::vector<GamePrintRun> print_runs_;
 static bool submitGamePrint(void* context,const GamePrintView& view) noexcept{try{return static_cast<Device*>(context)->appendGamePrint(view);}catch(...){return false;}}
 static bool flushGamePrintsSink(void* context) noexcept{try{return static_cast<Device*>(context)->flushGamePrints();}catch(...){return false;}}
 bool appendGamePrint(const GamePrintView& view){
  if(!scene_||native_batch_active_||!view.vertices||!view.count)return false;
  if(print_runs_.empty()){
   if(FAILED(flushPendingDraws())||FAILED(dd_->flushVisualUi())||FAILED(flushNativeVisual()))return false;
   native_sequence_=false;invalidateVisualTarget();
  }
  SurfaceImage* texture=textures_[0]?textures_[0]->image():nullptr;
  const UINT first=static_cast<UINT>(print_vertices_.size());
  print_vertices_.insert(print_vertices_.end(),view.vertices,view.vertices+view.count);
  if(!print_runs_.empty()){
   GamePrintRun& last=print_runs_.back();
   if(last.texture==texture&&last.object_to_clip==view.object_to_clip&&last.half_width==view.clip_half_width&&
      last.half_height==view.clip_half_height&&std::memcmp(&last.state,&state_,sizeof(FixedState))==0){last.count+=view.count;return true;}
  }
  GamePrintRun run;run.texture=texture;run.state=state_;run.object_to_clip=view.object_to_clip;
  run.half_width=view.clip_half_width;run.half_height=view.clip_half_height;run.first=first;run.count=view.count;
  print_runs_.push_back(run);return true;
 }
 bool flushGamePrints(){
  bool ok=true;
  if(scene_)for(const GamePrintRun& run:print_runs_)
   ok&=SUCCEEDED(dd_->renderer.draw_game_print_run(print_vertices_.data()+run.first,run.count,run.object_to_clip,
    run.half_width,run.half_height,run.state,run.texture));
  print_vertices_.clear();print_runs_.clear();return ok;
 }
 static bool submitGameGlyphs(void* context,const GameGlyphView& view) noexcept{try{return static_cast<Device*>(context)->drawGameGlyphs(view);}catch(...){return false;}}
 // Combat text draws with the state CWorldClient::Draw set before its call.
 bool drawGameGlyphs(const GameGlyphView& view){
  if(!scene_||native_batch_active_)return false;
  if(FAILED(flushPendingDraws())||FAILED(dd_->flushVisualUi())||FAILED(flushNativeVisual()))return false;
  native_sequence_=false;invalidateVisualTarget();
  return SUCCEEDED(dd_->renderer.draw_game_glyphs(view,state_,textures_[0]?textures_[0]->image():nullptr));
 }
 static bool beginNativeGeometryBatch(void* context,SemanticKind kind) noexcept{try{return static_cast<Device*>(context)->beginNativeBatch(kind);}catch(...){return false;}}
 static void endNativeGeometryBatch(void* context,bool commit) noexcept{try{static_cast<Device*>(context)->endNativeBatch(commit);}catch(...){}}
 static bool beginNativeObject(void* context) noexcept{try{return static_cast<Device*>(context)->beginNativeObjectTransaction();}catch(...){return false;}}
 static void endNativeObject(void* context,bool commit) noexcept{try{static_cast<Device*>(context)->endNativeObjectTransaction(commit);}catch(...){}}
 static bool nativeFrameState(void* context,NativeFrameState& output) noexcept{
  try{const Device* self=static_cast<const Device*>(context);if(!self||!self->scene_)return false;
   const auto& vp=self->state_.viewport;output.viewport={vp.dwX,vp.dwY,vp.dwWidth,vp.dwHeight};
    std::memcpy(output.view.data(),&self->state_.transform[D3DTRANSFORMSTATE_VIEW],sizeof(output.view));
    std::memcpy(output.projection.data(),&self->state_.transform[D3DTRANSFORMSTATE_PROJECTION],sizeof(output.projection));
    output.material=native_owned_fixed_state(self->state_);
    output.shadows_enabled=self->dd_->renderer.native_shadows_enabled();
   for(float value:output.view)if(!std::isfinite(value))return false;
   for(float value:output.projection)if(!std::isfinite(value))return false;return true;
  }catch(...){return false;}
 }
 static bool nativeWorldLighting(void* context,const NativeWorldLighting& lighting) noexcept{try{Device* self=static_cast<Device*>(context);return self&&self->scene_&&self->dd_->renderer.set_native_world_lighting(lighting);}catch(...){return false;}}
 static bool nativePointLights(void* context,const RetainedVisualFrame& frame) noexcept{try{Device* self=static_cast<Device*>(context);return self&&self->scene_&&self->dd_->renderer.set_native_point_lights(frame.world_point_lights,frame.time);}catch(...){return false;}}
 static bool beginNativePointShadowBatch(void* context,std::uint64_t& generation,bool& pressure) noexcept{
  try{Device* self=static_cast<Device*>(context);if(!self||!self->scene_||self->native_batch_active_)return false;
   if(FAILED(self->flushPendingDraws())||FAILED(self->dd_->flushVisualUi()))return false;
   self->native_sequence_=true;return self->dd_->renderer.begin_native_point_shadow_batch(generation,&pressure);
  }catch(...){return false;}
 }
 static bool allocateNativePointShadow(void* context,bool overlay,int& slot) noexcept{
  return static_cast<Device*>(context)->dd_->renderer.allocate_native_point_shadow(overlay,slot);
 }
 static void releaseNativePointShadow(void* context,std::uint64_t generation,int slot) noexcept{
  static_cast<Device*>(context)->dd_->renderer.release_native_point_shadow(generation,slot);
 }
 static void publishNativePointShadows(void* context,const std::vector<NativePointShadowPublication>& lights,
  const NativePointShadowStats& stats) noexcept{
  try{Device* self=static_cast<Device*>(context);if(self&&self->scene_)
   self->dd_->renderer.publish_native_point_shadows(lights,stats);
  }catch(...){}
 }
 static bool beginNativePointShadowFaces(void* context,std::uint32_t light,std::uint32_t first_face,
  const NativePointShadowFrame& faces) noexcept{
  try{Device* self=static_cast<Device*>(context);if(!self||!self->scene_||self->native_batch_active_)return false;

   self->native_sequence_=true;
   return self->dd_->renderer.begin_native_point_shadow_faces(light,first_face,faces);
  }catch(...){return false;}
 }
 static bool endNativePointShadowFaces(void* context,bool commit) noexcept{
  try{return static_cast<Device*>(context)->dd_->renderer.end_native_point_shadow_faces(commit);}catch(...){return false;}
 }
 static bool beginRainOcclusion(void* context,const NativeShadowSlice& slice,std::uint32_t side) noexcept{
  try{Device* self=static_cast<Device*>(context);if(!self||!self->scene_||self->native_batch_active_)return false;
   self->native_sequence_=true;
   return self->dd_->renderer.begin_native_rain_occlusion(slice,side);
  }catch(...){return false;}
 }
 static bool endRainOcclusion(void* context,bool commit) noexcept{
  try{return static_cast<Device*>(context)->dd_->renderer.end_native_rain_occlusion(commit);}catch(...){return false;}
 }
 static void nativeShadowCullProjection(void* context,NativeShadowCullProjection& projection) noexcept{
  Device* self=static_cast<Device*>(context);if(self)self->dd_->renderer.native_shadow_cull_projection(projection);
 }
 static bool beginNativeShadows(void* context,const NativeShadowFrame& frame,std::uint8_t& dirty) noexcept{
  try{Device* self=static_cast<Device*>(context);if(!self||!self->scene_||self->native_batch_active_)return false;
   if(FAILED(self->flushPendingDraws())||FAILED(self->dd_->flushVisualUi()))return false;
   self->native_sequence_=true;return self->dd_->renderer.begin_native_shadows(frame,dirty);
  }catch(...){return false;}
 }
 static bool endNativeShadows(void* context,bool commit) noexcept{
  try{return static_cast<Device*>(context)->dd_->renderer.end_native_shadows(commit);}catch(...){return false;}
 }
 static bool queueNativeStaticDepth(void* context,const RetainedShadowCasterValue* casters,
  std::uint32_t count,std::uint64_t store_revision,std::vector<RetainedShadowCasterValue>& missing) noexcept {
  try{Device* self=static_cast<Device*>(context);
   return self&&self->scene_&&self->dd_->renderer.queue_native_static_shadow_depth(
       casters,count,store_revision,missing);
  }catch(...){return false;}
 }
 static bool queueNativeTerrainBake(void* context,const NativeTerrainShadowEntry* entries,
  std::uint32_t count,std::vector<std::uint32_t>& absent) noexcept {
  try{Device* self=static_cast<Device*>(context);
   return self&&self->scene_&&self->dd_->renderer.queue_native_terrain_shadow_bake(entries,count,absent);
  }catch(...){return false;}
 }
 static void shadowSourceDiagnostics(void* context,std::uint64_t world_serial,
  const NativeShadowSourceDiagnostics& values) noexcept {
  try{Device* self=static_cast<Device*>(context);if(self&&self->scene_)
   self->dd_->renderer.record_native_shadow_source_diagnostics(world_serial,values);
  }catch(...){}
 }
 // The callback executes once with all queues drained at each target boundary.
 static bool submitCampPopup(void* context,CampRenderRequest request,CampPopupDraw draw,void* arguments) {
  return static_cast<Device*>(context)->drawCampPopup(request,draw,arguments);
 }
 bool drawCampPopup(CampRenderRequest request,CampPopupDraw draw,void* arguments) {
  if(request==CampRenderRequest::invalidate_popup){dd_->renderer.invalidate_camp_popup();return true;}
  if(request!=CampRenderRequest::begin_interface&&!draw)return false;
  if(!scene_||!target_||!target_->compositionOutput()||native_batch_active_||native_object_active_||camp_popup_active_)return false;
  if(FAILED(flushPendingDraws())||FAILED(dd_->flushVisualUi())||FAILED(flushNativeVisual()))return false;
  native_sequence_=false;
  if(request==CampRenderRequest::begin_interface)return SUCCEEDED(dd_->renderer.composite_world());
  if(FAILED(dd_->renderer.begin_camp_popup(request==CampRenderRequest::clear_popup)))return false;
  camp_popup_depth_={state_.render[D3DRENDERSTATE_ZENABLE],state_.render[D3DRENDERSTATE_ZWRITEENABLE],state_.render[D3DRENDERSTATE_ZFUNC]};
  state_.render[D3DRENDERSTATE_ZENABLE]=D3DZB_TRUE;
  state_.render[D3DRENDERSTATE_ZWRITEENABLE]=TRUE;
  state_.render[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;
  camp_popup_active_=true;
  return callCampPopup(draw,arguments);
 }
 bool callCampPopup(CampPopupDraw draw,void* arguments) {
  bool complete=false;
  // No C++ unwind objects in this frame; native SEH must restore the target too.
  __try {
   draw(arguments);
   complete=SUCCEEDED(flushPendingDraws())&&SUCCEEDED(flushNativeVisual());
  } __finally {
   if(!complete){
    if(native_object_active_){dd_->renderer.rollback_native(native_object_checkpoint_);native_object_active_=false;}
    native_batch_active_=false;clearPendingDraws();dd_->renderer.discard_native();
   }
   native_sequence_=false;
   state_.render[D3DRENDERSTATE_ZENABLE]=camp_popup_depth_[0];
   state_.render[D3DRENDERSTATE_ZWRITEENABLE]=camp_popup_depth_[1];
   state_.render[D3DRENDERSTATE_ZFUNC]=camp_popup_depth_[2];
   dd_->renderer.end_camp_popup(complete);
   camp_popup_active_=false;
  }
  return complete;
 }
 bool camp_popup_active_=false;
 std::array<DWORD,3> camp_popup_depth_{};
 bool beginNativeBatch(SemanticKind){if(!scene_||native_batch_active_)return false;HRESULT hr=flushPendingDraws();if(FAILED(hr))return false;if(FAILED(hr=dd_->flushVisualUi()))return false;if(FAILED(hr=flushNativeVisual()))return false;native_sequence_=true;native_batch_active_=true;return true;}
 void endNativeBatch(bool commit){if(!native_batch_active_)return;if(!commit)dd_->renderer.discard_native();native_batch_active_=false;}
 bool beginNativeObjectTransaction(){if(!scene_||native_object_active_)return false;if(!dd_->renderer.checkpoint_native(native_object_checkpoint_))return false;native_object_active_=true;return true;}
 void endNativeObjectTransaction(bool commit){if(!native_object_active_)return;if(!commit)dd_->renderer.rollback_native(native_object_checkpoint_);else dd_->renderer.release_native_checkpoint(native_object_checkpoint_);native_object_active_=false;}
 // Scene thread mode: a generic packet (legacy figure geometry, HUD figures)
 // the game needs only the selection result back from. That result is
 // computed here from the packet (Renderer::native_packet_extents), the
 // packet's arrays are copied off the game's memory and the submission is
 // queued with the device state and texture snapshots. Prepared, decoded,
 // world, terrain-page and surface-texture packets keep the synchronous path.
 struct ScenePacket {
  NativeGeometryView view;
  std::vector<NativePackedGroup> positions,normals;
  std::vector<std::uint32_t> diffuse,specular;
  std::vector<NativeTextureCoordinate> coordinates;
  std::vector<NativeVertexComponent> components;
  std::vector<std::uint16_t> indices;
  std::vector<NativeMorphOffset> morphs;
  std::vector<NativeOwnedTextureMip> mips;
  std::vector<std::vector<std::uint8_t>> mip_bytes;
 };
 static bool scenePacketEligible(const NativeGeometryView& view) noexcept{
  const bool prepared=view.prepared_mesh&&view.prepared_material&&view.prepared_identity.valid;
  return !prepared&&!view.decode&&!view.decode_colors&&!view.world.enabled&&!view.prebuilt_mesh&&!view.lods&&
   !view.component_parts&&!view.terrain_page&&!view.terrain_vertices&&!view.terrain_vertex_count&&
   !view.terrain_animation_phases&&!view.terrain_phase_values&&!view.owned_texture.surface&&
   !view.owned_texture.resource&&(!view.owned_texture.identity||view.owned_texture.mips)&&
   view.positions&&view.normals&&view.components&&view.texture_coordinates&&view.indices&&
   view.position_groups&&view.normal_groups&&view.component_count&&view.texture_coordinate_count&&view.index_count;
 }
 static std::shared_ptr<ScenePacket> copyScenePacket(const NativeGeometryView& source){
  auto packet=std::make_shared<ScenePacket>();
  auto& view=packet->view;view=source;
  if(!view.cache_identity&&!view.cache_pointers_valid){
   view.cache_pointers={reinterpret_cast<std::uintptr_t>(source.positions),reinterpret_cast<std::uintptr_t>(source.normals),
    reinterpret_cast<std::uintptr_t>(source.components),reinterpret_cast<std::uintptr_t>(source.texture_coordinates),
    reinterpret_cast<std::uintptr_t>(source.indices)};
   view.cache_pointers_valid=true;
  }
  const std::size_t colors=static_cast<std::size_t>(source.normal_groups)*4u;
  packet->positions.assign(source.positions,source.positions+source.position_groups);view.positions=packet->positions.data();
  packet->normals.assign(source.normals,source.normals+source.normal_groups);view.normals=packet->normals.data();
  if(source.diffuse){packet->diffuse.assign(source.diffuse,source.diffuse+colors);view.diffuse=packet->diffuse.data();}
  if(source.specular){packet->specular.assign(source.specular,source.specular+colors);view.specular=packet->specular.data();}
  packet->coordinates.assign(source.texture_coordinates,source.texture_coordinates+source.texture_coordinate_count);
  view.texture_coordinates=packet->coordinates.data();
  packet->components.assign(source.components,source.components+source.component_count);view.components=packet->components.data();
  packet->indices.assign(source.indices,source.indices+source.index_count);view.indices=packet->indices.data();
  if(source.morphs&&source.morph_count){packet->morphs.assign(source.morphs,source.morphs+source.morph_count);view.morphs=packet->morphs.data();}
  else{view.morphs=nullptr;view.morph_count=0;}
  if(source.owned_texture.identity&&source.owned_texture.mips){
   packet->mips.assign(source.owned_texture.mips,source.owned_texture.mips+source.owned_texture.mip_count);
   packet->mip_bytes.resize(packet->mips.size());
   for(std::size_t index=0;index<packet->mips.size();++index){
    auto& mip=packet->mips[index];
    if(mip.data&&mip.bytes){packet->mip_bytes[index].assign(mip.data,mip.data+mip.bytes);mip.data=packet->mip_bytes[index].data();}
   }
   view.owned_texture.mips=packet->mips.data();
  }
  return packet;
 }
 bool queueScenePacket(const NativeGeometryView& view,NativeGeometryResult& result){
  result={};
  invalidateVisualTarget();
  const FixedState submission_state=view.owned_fixed_state.enabled?native_owned_submission_state(state_,view.owned_fixed_state):state_;
  auto* bound=textures_[0]?textures_[0]->image():nullptr;
  camp_inventory_observe_texture(view,submission_state,bound);
  Surface* wound=nullptr;
  if(view.wound_surface_interface){
   wound=dynamic_cast<Surface*>(reinterpret_cast<IDirectDrawSurface7*>(view.wound_surface_interface));
   if(!wound)trace("Device.NativeWoundSurfaceRejected");
  }
  if(view.wound_only&&!wound)return false;
  // The synchronous result the renderer would return (the wound layer's
  // own result only for a wound-only packet; same packet, same viewport).
  if(view.flags&1u){
   RECT extents{};const HRESULT hr=dd_->renderer.native_packet_extents(view,submission_state,extents);
   if(FAILED(hr)){result.failure_hresult=hr;result.failure_operation="scene.screen_extents";return false;}
   result.visible=hr==S_OK;result.screen_extents_valid=true;result.screen_extents=extents;
  }
  auto sinks=std::make_shared<SceneSinks>();
  sinks->renderer=&dd_->renderer;sinks->state=state_;sinks->sequence=true;
  sinks->frame_valid=nativeFrameState(this,sinks->frame);
  sinks->bound=dd_->renderer.scene_surface(bound);
  if(wound){sinks->wound_interface=view.wound_surface_interface;sinks->wound=dd_->renderer.scene_surface(wound->image());}
  auto packet=copyScenePacket(view);
  dd_->renderer.scene_note_native();
  dd_->renderer.scene_post_frame([sinks,packet]{
   NativeGeometryResult ignored{};
   if(!SceneSinks::submit(sinks.get(),packet->view,ignored))
    Renderer::report_scene_failure(ignored.failure_operation?ignored.failure_operation:"scene.packet",
     ignored.failure_hresult?ignored.failure_hresult:E_FAIL);
  });
  return true;
 }
 bool queueNativeGeometry(const NativeGeometryView& view,NativeGeometryResult& result){
  camp::OutputScope output(target_&&target_->compositionOutput());
  result={};if(!scene_){result.failure_operation="proxy.scene";return false;}
  HRESULT hr=S_OK;if(!native_sequence_){hr=flushPendingDraws();if(FAILED(hr)){result.failure_hresult=hr;result.failure_operation="proxy.flush_pending_draws";return false;}if(FAILED(hr=dd_->flushVisualUi())){result.failure_hresult=hr;result.failure_operation="proxy.flush_ui";return false;}native_sequence_=true;}
  if(view.static_submission){
   const auto& request=*view.static_submission;invalidateVisualTarget();
   using Operation=NativeStaticSubmission::Operation;
   request.generation=dd_->renderer.native_static_generation();
   if(request.operation==Operation::generation)return true;
   if(request.operation==Operation::shadow_static||request.operation==Operation::shadow_dynamic){
    request.generation=dd_->renderer.set_native_shadow_phase(request.operation==Operation::shadow_static?1u:2u);return true;}
   if(request.operation==Operation::terrain_frame){
    if(!request.terrain_state){result.failure_hresult=E_INVALIDARG;result.failure_operation="proxy.terrain_frame";return false;}
    return SUCCEEDED(dd_->renderer.set_native_terrain_frame(*request.terrain_state,request.full_sun,request.layer_shifts));
   }
   if(request.operation==Operation::remove){dd_->renderer.remove_native_static_object(request.key,request.terrain);return true;}
   if(request.operation==Operation::flush){
    if(!request.world_to_clip){result.failure_hresult=E_INVALIDARG;result.failure_operation="proxy.static_clip";return false;}
    hr=request.terrain?dd_->renderer.flush_native_static_terrain(state_,*request.world_to_clip):
     dd_->renderer.flush_native_static(state_,*request.world_to_clip);
    if(FAILED(hr)){result.failure_hresult=hr;result.failure_operation="proxy.static_flush";
     char detail[96];std::snprintf(detail,sizeof(detail),"hr=%08lx draws=%zu",static_cast<unsigned long>(hr),dd_->renderer.native_static_draw_count());
     owned::bridge_append_probe_event("static_flush_failed",detail);return false;}
    return true;
   }
   bool registered=false;
   if(request.terrain){
    if(!request.terrain_view){result.failure_hresult=E_INVALIDARG;result.failure_operation="proxy.static_terrain_view";return false;}
    const auto& terrain=*request.terrain_view;
    const FixedState terrain_state=terrain.owned_fixed_state.enabled?native_owned_submission_state(state_,terrain.owned_fixed_state):state_;
    hr=dd_->renderer.set_native_static_terrain(request.key,terrain,terrain_state,
     terrain.owned_texture.identity?nullptr:(textures_[0]?textures_[0]->image():nullptr),registered,&result);
    request.registered=registered;
    return SUCCEEDED(hr);
   }
   if(!request.records){result.failure_hresult=E_INVALIDARG;result.failure_operation="proxy.static_records";return false;}
   hr=dd_->renderer.set_native_static_object(request.key,*request.records,state_,registered,&result);
   request.registered=registered;
   if(FAILED(hr)){char detail[160];std::snprintf(detail,sizeof(detail),"hr=%08lx op=%s",static_cast<unsigned long>(hr),
    result.failure_operation?result.failure_operation:"-");owned::bridge_append_probe_event("static_set_failed",detail);}
   return SUCCEEDED(hr);
  }
  if(view.depth_submission){
   invalidateVisualTarget();
   return SUCCEEDED(dd_->renderer.queue_native_depth(*view.depth_submission,state_,&result));
  }
  if(view.prepared_submission){
   const auto& submission=*view.prepared_submission;Surface* wound=nullptr;
   if(submission.wound_surface_interface){
    wound=dynamic_cast<Surface*>(reinterpret_cast<IDirectDrawSurface7*>(submission.wound_surface_interface));
    if(!wound){result.failure_hresult=E_INVALIDARG;result.failure_operation="proxy.prepared_wound";return false;}
   }
   invalidateVisualTarget();
   return SUCCEEDED(dd_->renderer.queue_native_records(submission,state_,wound?wound->image():nullptr,&result));
  }
  if(!camp::composing&&dd_->renderer.scene_queue()&&scenePacketEligible(view)){
   try{return queueScenePacket(view,result);}
   catch(...){result.failure_operation="scene.packet_copy";return false;}
  }
 const bool prepared=view.prepared_mesh&&view.prepared_material&&
   view.prepared_identity.valid;
  FixedState submission_state=view.owned_fixed_state.enabled?
   native_owned_submission_state(state_,view.owned_fixed_state):state_;
  const auto queue=[&](const NativeGeometryView& packet,const FixedState& state,
                       SurfaceImage* texture,NativeGeometryResult* output){
   invalidateVisualTarget();
   const auto* bound=textures_[0]?textures_[0]->image():nullptr;
   camp_inventory_observe_texture(packet,state,bound);
   NativeOwnedTextureView camp_texture{};
   if(camp::composing&&texture&&texture==bound&&!packet.owned_texture.identity&&
      !camp_inventory_texture(bound->id,bound->generation,camp_texture))
    return E_FAIL;
   if(camp_texture.identity){
    NativeGeometryView icon=packet;
    icon.owned_texture=camp_texture;
    icon.owned_fixed_state={};
    const HRESULT queued=dd_->renderer.queue_native_owned(icon,state,output);
    if(FAILED(queued))camp_inventory_texture_failed();
    return queued;
   }
   if(packet.owned_texture.identity)
    return dd_->renderer.queue_native_owned(packet,state,output);
   return prepared?dd_->renderer.queue_native_prepared(packet,state,texture,output):
    dd_->renderer.queue_native(packet,state,texture,output);
  };
  Surface* wound=nullptr;
  if(view.wound_surface_interface){
   wound=dynamic_cast<Surface*>(reinterpret_cast<IDirectDrawSurface7*>(view.wound_surface_interface));
   if(!wound)trace("Device.NativeWoundSurfaceRejected");
  }
  if(view.wound_only&&!wound)return false;
  const bool owned_depth=view.owned_fixed_state.enabled && view.contribution.known &&
   !view.contribution.color && view.contribution.light_mask;
  if(!wound)return SUCCEEDED(queue(view,submission_state,
   view.owned_texture.identity||owned_depth?nullptr:(textures_[0]?textures_[0]->image():nullptr),&result));
  if(!view.wound_only&&FAILED(queue(view,submission_state,textures_[0]?textures_[0]->image():nullptr,&result)))return false;
  FixedState wound_state=submission_state;
  wound_state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;
  wound_state.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_INVSRCALPHA;
  wound_state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=TRUE;
  wound_state.stage[0][D3DTSS_TEXCOORDINDEX]=0;
  NativeGeometryView wound_view=view;
  wound_view.wound_surface_interface=0;
  wound_view.wound_only=false;
  wound_view.owned_texture={};
  if(prepared)wound_view.prepared_material=view.prepared_wound_material;
  if(prepared&&!wound_view.prepared_material)return false;
  if(FAILED(prepared?dd_->renderer.queue_native_prepared(wound_view,wound_state,wound->image(),view.wound_only?&result:nullptr):
                    dd_->renderer.queue_native(wound_view,wound_state,wound->image(),view.wound_only?&result:nullptr))){trace("Device.NativeWoundQueueFailed");return !view.wound_only;}
  return true;
 }
  HRESULT queueDraw(D3DPRIMITIVETYPE,DWORD,const void*,DWORD,const WORD*,DWORD);
 HRESULT flushPendingDraws(){if(pending_draws_.empty())return D3D_OK;invalidateVisualTarget();HRESULT hr=dd_->renderer.draw_frame_take(pending_draws_,pending_vertex_bytes_,pending_indices_);clearPendingDraws();return hr;}
 void clearPendingDraws(){for(auto* texture:pending_texture_refs_)if(texture)texture->Release();pending_texture_refs_.clear();pending_draws_.clear();pending_vertex_bytes_.clear();pending_indices_.clear();}
 HRESULT unsupported(const char* n){trace(n);return E_NOTIMPL;}
 std::atomic<ULONG> refs_{1};DD* dd_{};Surface* target_{};Surface* composition_target_{};std::array<Surface*,8> textures_{};FixedState state_{},recording_state_{};bool scene_{},recording_{},native_sequence_{},native_batch_active_{},native_object_active_{};Renderer::NativeQueueCheckpoint native_object_checkpoint_{};std::array<bool,256> recording_render_mask_{};std::array<std::array<bool,32>,8> recording_stage_mask_{};std::array<Surface*,8> recording_textures_{};std::array<bool,8> recording_texture_mask_{};std::array<bool,4> recording_transform_mask_{};bool recording_viewport_mask_{},recording_material_mask_{};DWORD next_block_{1};std::unordered_map<DWORD,StateBlock> blocks_{};std::vector<LegacyDrawPacket> pending_draws_{};std::vector<std::uint8_t> pending_vertex_bytes_{};std::vector<WORD> pending_indices_{};std::vector<Surface*> pending_texture_refs_{};
};

void DD::applyDisplay(const owned::DisplaySettings& settings){
 const auto reject=[&](HRESULT hr,const char* text){renderer.finish_display_apply(settings,hr,text);};
 if(!hwnd || !active_device || !active_device->displayResizeIdle() || renderer.has_pending_ui())
  return reject(E_PENDING,"Wait for a completed frame, then Apply again.");
 if(const char* blocker=owned::widescreen_display_blocker())return reject(E_PENDING,blocker);
 for(Surface* surface:{primary,active_target,active_target?active_target->displayDepth():nullptr})
  if(surface && !surface->displayResizeIdle())return reject(E_PENDING,"A native output surface is still locked. Apply after the frame finishes.");
 const LONG_PTR old_style=GetWindowLongPtrA(hwnd,GWL_STYLE),old_ex_style=GetWindowLongPtrA(hwnd,GWL_EXSTYLE);
 RECT old_rect{};if(!GetWindowRect(hwnd,&old_rect))return reject(E_FAIL,"The window position could not be saved. Display unchanged.");
 const auto restore_window=[&]{
  if(!set_window_style(hwnd,GWL_STYLE,old_style) || !set_window_style(hwnd,GWL_EXSTYLE,old_ex_style))return false;
  return SetWindowPos(hwnd,old_ex_style&WS_EX_TOPMOST?HWND_TOPMOST:HWND_NOTOPMOST,old_rect.left,old_rect.top,old_rect.right-old_rect.left,old_rect.bottom-old_rect.top,
   SWP_FRAMECHANGED|SWP_NOOWNERZORDER|SWP_NOACTIVATE)!=FALSE;
 };
 OutputWindowPlacement placement{};
 if(!plan_output_window(hwnd,settings,placement))return reject(E_FAIL,"The requested window size could not be planned. Display unchanged.");
 // Preserve the stock-fit font/raster unit on either side of 4:3. Only the
 // unconstrained canvas span changes to match the actual fitted output.
 const double native_unit=(std::min)(double(width)/800.,double(height)/600.);
 const double aspect=double(placement.client.width)/placement.client.height;
 const double canvas_x=native_unit*(std::max)(800.,600.*aspect);
 const double canvas_y=native_unit*(std::max)(600.,800./aspect);
 if(!std::isfinite(canvas_x) || !std::isfinite(canvas_y) || canvas_x<1. || canvas_y<1. ||
    canvas_x>(std::numeric_limits<LONG>::max)() || canvas_y>(std::numeric_limits<LONG>::max)())
  return reject(E_INVALIDARG,"The requested native canvas is out of range. Display unchanged.");
 const UINT canvas_width=UINT(std::lround(canvas_x)),canvas_height=UINT(std::lround(canvas_y));
 if(const char* blocker=owned::widescreen_display_span_blocker(canvas_width,canvas_height)){
  return reject(E_INVALIDARG,blocker);
 }
 RECT client{};
 if(!apply_output_window(hwnd,placement) || !GetClientRect(hwnd,&client) ||
    client.right!=placement.client.width || client.bottom!=placement.client.height){
  const bool restored=restore_window();return reject(E_FAIL,restored?"The requested window did not match its planned client size. Previous display restored.":"Window Apply failed, and its previous position could not be restored.");
 }
 const HRESULT hr=renderer.resize(UINT(client.right),UINT(client.bottom));
 if(FAILED(hr)){
  const bool restored=restore_window();return reject(hr,restored?"Output resize failed. Previous window restored; see the renderer log.":"Output resize failed, and the window could not be restored.");
 }
 const UINT old_width=width,old_height=height;
 if(canvas_width!=width || canvas_height!=height){
  width=canvas_width;height=canvas_height;
  if(primary)primary->resizeDisplayCanvas(width,height);
  if(active_target && active_target!=primary)active_target->resizeDisplayCanvas(width,height);
  if(auto* depth=active_target?active_target->displayDepth():nullptr)depth->resizeDisplayCanvas(width,height);
  active_device->resizeDisplayViewport(old_width,old_height,width,height);
  owned::widescreen_refresh_display(width,height);
 }
 camp_inventory_output(width,height);
 char result[160];std::snprintf(result,sizeof(result),"Applied %ld x %ld %s.",client.right,client.bottom,
  settings.mode==owned::DisplayMode::windowed?"windowed":"borderless");
 trace("Window.LiveApply","%s native_canvas=%lux%lu",result,width,height);renderer.finish_display_apply(settings,S_OK,result);
}

namespace {
Surface* completed_surface_receiver(IDirectDrawSurface7* surface) noexcept {
    __try { return dynamic_cast<Surface*>(surface); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
}

bool proxy_completed_visual_surface(IDirectDrawSurface7* surface,
    std::shared_ptr<const OwnedVisualSurface>& value) noexcept {
    value.reset();
    auto* receiver = completed_surface_receiver(surface);
    return receiver && receiver->completedVisualValue(value);
}

bool proxy_publish_camp_texture(IDirectDrawSurface7* surface,
    const PreparedCampIcon& icon,
    std::shared_ptr<const OwnedVisualSurface>& value) noexcept {
    value.reset();
    auto* receiver = completed_surface_receiver(surface);
    return receiver && receiver->publishCampIcon(icon,value);
}

bool Surface::publishCampIcon(const PreparedCampIcon& icon,
    std::shared_ptr<const OwnedVisualSurface>& value) noexcept {
    // Admit the worker's completed GPU home. No CPU bytes, alpha scan,
    // conversion, upload, native decoder or immediate context call here.
    const auto& prepared = icon.description;
    const auto& format = image_.format;
    if ((image_.width != 256 && image_.width != 1024 && image_.width != 2048) ||
        image_.height != image_.width || image_.pitch != image_.width * 2 ||
        format.dwRGBBitCount != 16 || format.dwRBitMask != 0x7c00 ||
        format.dwGBitMask != 0x03e0 || format.dwBBitMask != 0x001f ||
        format.dwRGBAlphaBitMask != 0x8000 || prepared.width != image_.width ||
        prepared.height != image_.height || prepared.pitch != image_.pitch ||
        !icon.texture || !icon.srv ||
        (!prepared.alpha_fully_opaque && (!prepared.alpha_cut_mask ||
            prepared.alpha_cut_mask->size() != (std::size_t(image_.width)*image_.height+7)/8)) ||
        !image_.pixels.empty() || image_.texture || image_.gpu_captured || locked_ || dc_active_) return false;
    ComPtr<ID3D11Device> device; icon.texture->GetDevice(&device);
    if (device.Get() != owner_->renderer.resource_device()) return false;
    OwnedVisualSurface description = prepared;
    description.id = image_.id; description.generation = image_.generation;
    description.format = format;
    if (!retained_visual_complete_surface(description,nullptr,0,value,true)) return false;
    image_.texture = icon.texture; image_.srv = icon.srv;
    image_.uploaded_generation = image_.generation;
    image_.uploaded_shape = surface_upload_shape(image_);
    image_.published_generation = image_.generation;
    image_.cpu_released = true; image_.visual_source = true; image_.mip_source_used = true;
    ++image_.texture_serial; clear_surface_dirty(image_);
    visual_cpu_current_ = true; visual_source_observed_ = true;
    update_surface_memory_accounting(image_);
    if (owner_->renderer.scene_queue()) {
        owner_->renderer.scene_publish_surface_texture(image_);
        std::lock_guard<std::mutex> guard(g_scene_surface_lock);
        try { g_scene_surface_textures[image_.id] = {image_.generation, image_.texture}; } catch (...) {}
    }
    return true;
}

void proxy_retire_camp_texture(std::uint64_t id,std::uint64_t generation) noexcept {
    const auto found=g_surfaces_by_id.find(id);
    if(found!=g_surfaces_by_id.end())
        found->second->owner()->renderer.retire_camp_texture(id,generation);
}

// Completed visual sources carry no pixels. The surface's own texture is their
// GPU home: make it current for the published generation and hand it out.
HRESULT resolve_proxy_surface_texture(void* context,ID3D11Device*,std::uint64_t id,
    std::uint64_t generation,ComPtr<ID3D11Texture2D>& texture,SurfaceImage*& image_out){
    texture.Reset();image_out=nullptr;
    auto* owner=static_cast<DD*>(context);
    if(owner&&owner->renderer.on_scene_thread()){
        std::lock_guard<std::mutex> guard(g_scene_surface_lock);
        const auto found=g_scene_surface_textures.find(id);
        if(found!=g_scene_surface_textures.end()&&found->second.generation==generation&&found->second.texture){
            texture=found->second.texture;return S_OK;
        }
        try{
            if(std::find(g_scene_surface_requests.begin(),g_scene_surface_requests.end(),std::make_pair(id,generation))==g_scene_surface_requests.end())
                g_scene_surface_requests.emplace_back(id,generation);
        }catch(...){}
        return E_PENDING;
    }
    const auto found=g_surfaces_by_id.find(id);
    if(!owner||found==g_surfaces_by_id.end()||found->second->owner()!=owner)return E_FAIL;
    auto& image=*found->second->image();
    if(image.generation!=generation||image.gpu_captured)return E_FAIL;
    // An open Lock/DC session has not finished this generation's bytes yet.
    if(image.cpu_access_active&&image.uploaded_generation!=image.generation)return E_PENDING;
    const HRESULT hr=owner->renderer.upload(image);
    if(FAILED(hr))return hr;
    texture=image.texture;
    if (!texture) return E_FAIL;
    owner->renderer.sampled_surface_pixels(image,true);
    image_out=&image;
    return S_OK;
}

// Game thread, scene mode: the published generation's GPU home, current now,
// for the scene thread's resolver.
void register_scene_surface_texture(DD& owner,SurfaceImage& image) noexcept{
    if(!owner.renderer.scene_queue()||image.gpu_captured)return;
    if(image.uploaded_generation!=image.generation||!image.texture)(void)owner.renderer.upload(image);
    if(image.uploaded_generation!=image.generation||!image.texture)return;
    // From here the game thread never writes this texture in place (the owned
    // cache may share it): its next write detaches first.
    owner.renderer.scene_publish_surface_texture(image);
    try{std::lock_guard<std::mutex> guard(g_scene_surface_lock);g_scene_surface_textures[image.id]={image.generation,image.texture};}catch(...){}
}

void DD::serviceSceneSurfaceRequests() noexcept{
    std::vector<std::pair<std::uint64_t,std::uint64_t>> requests;
    {std::lock_guard<std::mutex> guard(g_scene_surface_lock);requests.swap(g_scene_surface_requests);}
    for(const auto& request:requests){
        const auto found=g_surfaces_by_id.find(request.first);
        if(found==g_surfaces_by_id.end()||found->second->owner()!=this)continue;
        auto& image=*found->second->image();
        if(image.generation!=request.second||image.gpu_captured)continue;
        // An open Lock/DC session has not finished this generation's bytes yet.
        if(image.cpu_access_active&&image.uploaded_generation!=image.generation)continue;
        register_scene_surface_texture(*this,image);
    }
}

bool Surface::completedVisualValue(std::shared_ptr<const OwnedVisualSurface>& value) noexcept {
    // Keep an existing publication alive while replacing the caller's reference.
    auto cached = retained_visual_surface_value(image_.id, image_.generation);
    value.reset();
    // Publication/composition reads alpha and cut-mask facts on the CPU.
    // Establish this source role before any upload can discard its bytes.
    // A generation whose publication fails no longer holds its bytes for it.
    image_.visual_source = true;
    const auto reject = [this]() noexcept {
        image_.visual_source = false; held_publication_.reset(); return false; };
    if (!visualCpuCurrent()) return reject();
    value = std::move(cached);
    if (value) {
        held_publication_ = value;
        image_.published_generation = image_.generation;
        register_scene_surface_texture(*owner_, image_);
        releaseCpuPixels();
        return true;
    }
    if (FAILED(restoreReleased(false))) return reject();
    if (!visualCpuCurrent() ||
        !image_.id || !image_.generation || image_.pixels.empty() ||
        !image_.width || !image_.height || image_.width > 8192 || image_.height > 8192 ||
        image_.pitch <= 0 || image_.format.dwSize != sizeof(DDPIXELFORMAT)) return reject();
    const auto& format = image_.format;
    const bool blocks = block_compressed(format);
    constexpr DWORD unsupported_rgb = DDPF_FOURCC | DDPF_PALETTEINDEXED1 |
        DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXED8 | DDPF_PALETTEINDEXEDTO8;
    if (!blocks && (!(format.dwFlags & DDPF_RGB) || (format.dwFlags & unsupported_rgb) ||
        (format.dwRGBBitCount != 16 && format.dwRGBBitCount != 24 && format.dwRGBBitCount != 32)))
        return reject();
    const std::uint64_t rows = blocks ? (image_.height + 3u) / 4u : image_.height;
    const std::uint64_t row_bytes = blocks ?
        static_cast<std::uint64_t>((image_.width + 3u) / 4u) * block_bytes(format) :
        static_cast<std::uint64_t>(image_.width) * (format.dwRGBBitCount / 8u);
    const std::uint64_t bytes = static_cast<std::uint64_t>(image_.pitch) * rows;
    // Match the owned immutable source's existing 64 MiB per-resource bound.
    // This check precedes allocation; it never manufactures missing CPU data.
    if (row_bytes > static_cast<std::uint64_t>(image_.pitch) ||
        bytes > 64u * 1024u * 1024u || bytes != image_.pixels.size()) return reject();
    OwnedVisualSurface description;
    description.id = image_.id;
    description.generation = image_.generation;
    description.width = image_.width;
    description.height = image_.height;
    description.pitch = image_.pitch;
    description.format = format;
    description.has_color_key = image_.has_color_key;
    description.color_key = image_.color_key;
    description.static_mip_hint = image_.static_mip_hint;
    image_.mip_source_used = true;
    if (!retained_visual_complete_surface(description, std::as_const(image_.pixels).data(), image_.pixels.size(), value)) {
        retained_visual_retire_surface(image_.id);
        return reject();
    }
    visual_source_observed_ = true;
    held_publication_ = value;
    image_.published_generation = image_.generation;
    register_scene_surface_texture(*owner_, image_);
    releaseCpuPixels();
    return true;
}

void Surface::completedVisualMutation() noexcept {
    if (!visual_source_observed_) return;
    PostEngineSurfaceTimingScope timing(
        PostEngineSurfaceTimingKind::completed_surface);
    std::shared_ptr<const OwnedVisualSurface> next;
    if (!completedVisualValue(next)) retained_visual_retire_surface(image_.id);
    timing.finish(image_.pixels.size());
}

bool Surface::visualCpuCurrent() const noexcept {
    const bool output = this == owner_->active_target || this == owner_->primary;
    return visual_cpu_current_ && !locked_ && !dc_active_ && !lost_ &&
        !image_.gpu_captured && !(output && (owner_->renderer.has_pending_ui() ||
            (owner_->active_device && owner_->active_device->visualWritesPending())));
}

void Surface::invalidateVisualCpu() noexcept {
    if (!visual_cpu_current_) return;
    visual_cpu_current_ = false;
    held_publication_.reset();
    if (visual_source_observed_) {
        PostEngineSurfaceTimingScope timing(
            PostEngineSurfaceTimingKind::completed_surface);
        retained_visual_retire_surface(image_.id);
        timing.finish(image_.pixels.size());
    }
}

void Surface::invalidateVisualTarget() noexcept {
    invalidateVisualCpu();
    // The proxy's display/active-target surfaces name the same renderer output.
    owner_->invalidateVisualOutput();
}

void Surface::completedVisualCpuWrite(bool full_surface, bool source_current) noexcept {
    // Partial writes cannot repair pixels left stale by a GPU write. Copying an
    // unavailable source also cannot confer authority on its destination.
    visual_cpu_current_ = source_current && (visual_cpu_current_ || full_surface);
    completedVisualMutation();
}

HRESULT Surface::legacyCpuAccess() {
    // Only replaces the existing Lock/DC/copy access sites. Publication never
    // invokes this method or requests a new readback to manufacture a value.
    if (!image_.gpu_captured) return ensurePixels();
    // Capture invalidates this bit before every GPU write. An exact readback
    // makes the existing CPU backing current without retiring GPU provenance.
    // Publication has stricter eligibility and still rejects captured images.
    const size_t required = static_cast<size_t>(image_.pitch) *
        surface_rows(image_.height, image_.format);
    if (visual_cpu_current_ && !image_.cpu_released &&
        image_.pixels.size() >= required) return DD_OK;
    const HRESULT result = owner_->checkDevice(owner_->renderer.readback(image_));
    if (SUCCEEDED(result)) visual_cpu_current_ = true;
    return result;
}

Surface::Surface(DD* o,const DDSURFACEDESC2& d):owner_(o),desc_(d){g_surface_vtable=*reinterpret_cast<void* const*>(static_cast<IDirectDrawSurface7*>(this));image_.static_mip_hint=(d.ddsCaps.dwCaps&DDSCAPS_TEXTURE)&&!(d.ddsCaps.dwCaps&(DDSCAPS_3DDEVICE|DDSCAPS_PRIMARYSURFACE|DDSCAPS_BACKBUFFER))&&(d.ddsCaps.dwCaps2&DDSCAPS2_HINTSTATIC)&&!(d.ddsCaps.dwCaps2&DDSCAPS2_HINTDYNAMIC);image_.gpu_primary=(d.ddsCaps.dwCaps&DDSCAPS_TEXTURE)&&!(d.ddsCaps.dwCaps&(DDSCAPS_SYSTEMMEMORY|DDSCAPS_3DDEVICE|DDSCAPS_PRIMARYSURFACE|DDSCAPS_BACKBUFFER|DDSCAPS_ZBUFFER));owner_->AddRef();image_.id=++g_next_surface_id;try{g_surfaces_by_id[image_.id]=this;}catch(...){image_.gpu_primary=false;}if(!desc_.dwSize)desc_.dwSize=sizeof(desc_);image_.width=std::max<DWORD>(1,desc_.dwWidth?desc_.dwWidth:owner_->width);image_.height=std::max<DWORD>(1,desc_.dwHeight?desc_.dwHeight:owner_->height);image_.format=(desc_.dwFlags&DDSD_PIXELFORMAT)?desc_.ddpfPixelFormat:bgra32();image_.pitch=surface_pitch(image_.width,image_.format);desc_.dwWidth=image_.width;desc_.dwHeight=image_.height;desc_.lPitch=image_.pitch;desc_.ddpfPixelFormat=image_.format;desc_.dwFlags|=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PITCH|DDSD_PIXELFORMAT;if(primary())owner_->primary=this;trace("Surface.Create","id=%llu caps=%08lx %ux%u flags=%08lx fourcc=%08lx bpp=%lu pitch=%ld backing=lazy",(unsigned long long)image_.id,desc_.ddsCaps.dwCaps,image_.width,image_.height,image_.format.dwFlags,image_.format.dwFourCC,image_.format.dwRGBBitCount,image_.pitch);}
Surface::~Surface(){if(visual_source_observed_){retained_visual_retire_surface(image_.id);visual_source_observed_=false;}if(dc_layer_){ui_layer_abort(layer_dc_);dc_layer_=false;dc_active_=false;image_.cpu_access_active=false;}if(dc_active_)ReleaseDC(dc_owned_text_?owned_text_dc_:dc_);if(owned_text_dc_){bridge_retire_owned_text_surface(image_.id);SelectObject(owned_text_dc_,owned_text_old_bitmap_);DeleteObject(owned_text_bitmap_);DeleteDC(owned_text_dc_);}freeDcStorage();g_surfaces_by_id.erase(image_.id);scene_surface_texture_erase(image_.id);if(attached_)attached_->Release();if(owner_->primary==this)owner_->primary=nullptr;owner_->renderer.retire_surface(image_.id);release_surface_memory_accounting(image_);owner_->Release();trace("Surface.Destroy");}
HRESULT Surface::ensurePixels(){if(image_.cpu_released)return restoreReleased();if(!image_.pixels.empty())return DD_OK;const auto bytes=(size_t)image_.pitch*surface_rows(image_.height,image_.format);try{image_.pixels.resize(bytes);}catch(const std::bad_alloc&){trace("Surface.AllocationFailed","%ux%u bytes=%llu",image_.width,image_.height,(unsigned long long)bytes);return DDERR_OUTOFMEMORY;}update_surface_memory_accounting(image_);return DD_OK;}
HRESULT Surface::ensureDcStorage(){if(dc_&&dc_bitmap_&&dc_bits_)return DD_OK;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=(LONG)image_.width;info.bmiHeader.biHeight=-(LONG)image_.height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;dc_=CreateCompatibleDC(nullptr);if(!dc_)return DDERR_GENERIC;dc_bitmap_=CreateDIBSection(dc_,&info,DIB_RGB_COLORS,&dc_bits_,nullptr,0);if(!dc_bitmap_||!dc_bits_){if(dc_bitmap_)DeleteObject(dc_bitmap_);DeleteDC(dc_);dc_=nullptr;dc_bitmap_=nullptr;dc_bits_=nullptr;return DDERR_GENERIC;}dc_old_bitmap_=SelectObject(dc_,dc_bitmap_);try{g_dc_holders.push_back(this);}catch(...){freeDcStorage();return DDERR_OUTOFMEMORY;}return DD_OK;}
HRESULT Surface::ensurePixelsForWrite(bool full_overwrite){
 // A complete overwrite needs no readback of the released contents.
 if(full_overwrite&&image_.cpu_released){
  try{image_.pixels.resize((size_t)image_.pitch*surface_rows(image_.height,image_.format));}catch(const std::bad_alloc&){return DDERR_OUTOFMEMORY;}
  image_.cpu_released=false;update_surface_memory_accounting(image_);return DD_OK;
 }
 return ensurePixels();
}
void Surface::freeDcStorage() noexcept{dib_generation_=0;if(!dc_)return;g_dc_holders.erase(std::remove(g_dc_holders.begin(),g_dc_holders.end(),this),g_dc_holders.end());SelectObject(dc_,dc_old_bitmap_);DeleteObject(dc_bitmap_);DeleteDC(dc_);dc_=nullptr;dc_bitmap_=nullptr;dc_old_bitmap_=nullptr;dc_bits_=nullptr;}
HRESULT Surface::restoreReleased(bool pin){
 if(!image_.cpu_released&&!pin)return DD_OK;
 if(image_.cpu_released&&!owner_->renderer.ready())return DDERR_SURFACELOST;
 return owner_->renderer.restore_surface_pixels(image_,pin);
}
void Surface::releaseCpuPixels() noexcept{if(owner_->renderer.ready())owner_->renderer.release_surface_pixels(image_);}
void Surface::finishCpuWrite() noexcept{
 // A pinned surface keeps its CPU copy, so its upload stays deferred to use.
 if(!image_.gpu_primary||image_.cpu_pinned||image_.cpu_access_active||!owner_->renderer.ready())return;
 if(SUCCEEDED(owner_->renderer.upload(image_)))owner_->renderer.release_surface_pixels(image_);
}
HRESULT Surface::ensureOwnedTextDc(){if(owned_text_dc_&&owned_text_bitmap_&&owned_text_bits_)return DD_OK;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=1;info.bmiHeader.biHeight=-1;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;owned_text_dc_=CreateCompatibleDC(nullptr);if(!owned_text_dc_)return DDERR_GENERIC;owned_text_bitmap_=CreateDIBSection(owned_text_dc_,&info,DIB_RGB_COLORS,&owned_text_bits_,nullptr,0);if(!owned_text_bitmap_||!owned_text_bits_){if(owned_text_bitmap_)DeleteObject(owned_text_bitmap_);DeleteDC(owned_text_dc_);owned_text_dc_=nullptr;owned_text_bitmap_=nullptr;owned_text_bits_=nullptr;return DDERR_GENERIC;}owned_text_old_bitmap_=SelectObject(owned_text_dc_,owned_text_bitmap_);if(!owned_text_old_bitmap_||owned_text_old_bitmap_==HGDI_ERROR){DeleteObject(owned_text_bitmap_);DeleteDC(owned_text_dc_);owned_text_dc_=nullptr;owned_text_bitmap_=nullptr;owned_text_old_bitmap_=nullptr;owned_text_bits_=nullptr;return DDERR_GENERIC;}return DD_OK;}
HRESULT Surface::QueryInterface(REFIID r,void** o){trace_iid("Surface.QueryInterface",r);if(!o)return E_POINTER;*o=nullptr;if(r==IID_IUnknown||r==IID_IDirectDrawSurface7){*o=this;AddRef();return S_OK;}return E_NOINTERFACE;}
HRESULT Surface::AddAttachedSurface(LPDIRECTDRAWSURFACE7 s){trace("Surface.AddAttachedSurface");auto* p=dynamic_cast<Surface*>(s);if(!p)return DDERR_INVALIDOBJECT;if(attached_)attached_->Release();attached_=p;attached_->AddRef();return DD_OK;}
HRESULT Surface::DeleteAttachedSurface(DWORD,LPDIRECTDRAWSURFACE7 s){trace("Surface.DeleteAttachedSurface");if(attached_!=s)return DDERR_NOTFOUND;attached_->Release();attached_=nullptr;return DD_OK;}
HRESULT Surface::EnumAttachedSurfaces(LPVOID c,LPDDENUMSURFACESCALLBACK7 cb){trace("Surface.EnumAttachedSurfaces");if(!cb)return DDERR_INVALIDPARAMS;if(attached_){DDSURFACEDESC2 d{};attached_->GetSurfaceDesc(&d);cb(attached_,&d,c);}return DD_OK;}
HRESULT Surface::GetAttachedSurface(LPDDSCAPS2 caps,LPDIRECTDRAWSURFACE7* out){trace("Surface.GetAttachedSurface");if(!out)return DDERR_INVALIDPARAMS;*out=nullptr;if(!attached_)return DDERR_NOTFOUND;if(caps&&(attached_->desc_.ddsCaps.dwCaps&caps->dwCaps)!=caps->dwCaps)return DDERR_NOTFOUND;*out=attached_;attached_->AddRef();return DD_OK;}
bool Surface::compositionOutput()const noexcept {
 // The native device is created against CDXDirect3D::GetRenderSurface;
 // retain that original output owner across temporary SetRenderTarget calls.
 return !(desc_.ddsCaps.dwCaps&DDSCAPS_TEXTURE)&&(primary()||
  (owner_->active_device&&owner_->active_device->compositionTarget(this)));
}
HRESULT Surface::GetCaps(LPDDSCAPS2 c){if(!c)return DDERR_INVALIDPARAMS;*c=desc_.ddsCaps;return DD_OK;}
HRESULT Surface::GetPixelFormat(LPDDPIXELFORMAT f){if(!f)return DDERR_INVALIDPARAMS;*f=image_.format;return DD_OK;}
HRESULT Surface::GetSurfaceDesc(LPDDSURFACEDESC2 d){if(!d)return DDERR_INVALIDPARAMS;DWORD size=d->dwSize;*d=desc_;d->dwSize=size?size:sizeof(*d);d->lpSurface=locked_?image_.pixels.data():nullptr;return DD_OK;}
HRESULT Surface::beginDibSession(){HRESULT hr=restoreReleased(false);if(FAILED(hr))return hr;hr=legacyCpuAccess();if(FAILED(hr))return hr;hr=ensureDcStorage();if(FAILED(hr))return hr;dc_last_present_=g_present_serial;image_.cpu_access_active=true;const auto& format=image_.format;const DWORD bytes=pixel_bytes(format);auto* output=static_cast<DWORD*>(dc_bits_);const bool native_bgra=format.dwRGBBitCount==32&&format.dwRBitMask==0x00ff0000&&format.dwGBitMask==0x0000ff00&&format.dwBBitMask==0x000000ff&&format.dwRGBAlphaBitMask==0xff000000;if(native_bgra){const size_t row_bytes=(size_t)image_.width*4;for(UINT y=0;y<image_.height;++y)std::memcpy(output+(size_t)y*image_.width,image_.pixels.data()+(size_t)y*image_.pitch,row_bytes);}else for(UINT y=0;y<image_.height;++y)for(UINT x=0;x<image_.width;++x){DWORD raw{};std::memcpy(&raw,image_.pixels.data()+(size_t)y*image_.pitch+(size_t)x*bytes,std::min<DWORD>(bytes,4));const BYTE r=expand_mask(raw,format.dwRBitMask,0),g=expand_mask(raw,format.dwGBitMask,0),b=expand_mask(raw,format.dwBBitMask,0),a=expand_mask(raw,format.dwRGBAlphaBitMask,255);output[(size_t)y*image_.width+x]=(DWORD(a)<<24)|(DWORD(r)<<16)|(DWORD(g)<<8)|b;}return DD_OK;}
HRESULT Surface::GetDC(HDC* out){trace("Surface.GetDC","%ux%u flags=%08lx fourcc=%08lx bpp=%lu",image_.width,image_.height,image_.format.dwFlags,image_.format.dwFourCC,image_.format.dwRGBBitCount);if(!out)return DDERR_INVALIDPARAMS;*out=nullptr;if(dc_active_)return DDERR_DCALREADYCREATED;if(locked_||block_compressed(image_.format)||!(image_.format.dwFlags&DDPF_RGB))return DDERR_UNSUPPORTED;const bool owned_text=bridge_begin_character_pane_getdc(static_cast<IDirectDrawSurface7*>(this),_ReturnAddress(),image_.id,image_.width,image_.height,owner_->renderer.owned_text_raster_scale());if(owned_text){const HRESULT hr=ensureOwnedTextDc();if(FAILED(hr))return hr;dc_active_=true;dc_owned_text_=true;*out=owned_text_dc_;return DD_OK;}if(ownedLayerClaim(_ReturnAddress())){const HDC dc=ui_layer_begin(this,&Surface::promoteLayer);if(dc){dc_active_=true;dc_layer_=true;layer_dc_=dc;*out=dc;return DD_OK;}bridge_append_probe_event("owned_ui_text_omitted","recording_dc_unavailable");}const HRESULT hr=beginDibSession();if(FAILED(hr))return hr;dc_active_=true;dc_owned_text_=false;*out=dc_;return DD_OK;}
HRESULT Surface::ReleaseDC(HDC value){trace("Surface.ReleaseDC");if(!dc_active_)return DDERR_NODC;if(dc_layer_)return releaseLayerDc(value);if(dc_owned_text_){if(!owned_text_dc_||value!=owned_text_dc_)return DDERR_NODC;bridge_finish_character_pane_releasedc(static_cast<IDirectDrawSurface7*>(this),_ReturnAddress(),image_.id,image_.generation);dc_active_=false;dc_owned_text_=false;return DD_OK;}if(!dc_||value!=dc_)return DDERR_NODC;dc_active_=false;
 // A GDI session publishes only the pixels it changed. Interface text redrawn
 // identically every frame (camp/store grids, cards, strips) then keeps its
 // generation, so converted and uploaded copies stay current. A GPU-captured
 // surface keeps the full publication that retires its GPU provenance.
 if(!image_.gpu_captured){RECT changed{};const bool any=packDibSession(&changed);dib_generation_=0;finishLayerWrite(any,changed);return DD_OK;}
 packDibSession(nullptr);image_.cpu_access_active=false;image_.gpu_captured=false;mark_surface_dirty_full(image_);advance_surface_generation(image_,nullptr);++unique_;completedVisualMutation();finishCpuWrite();return DD_OK;}
// Legacy DC sessions publish the whole DIB; owned layers write back only the
// pixels their replay changed and report those bounds.
bool Surface::packDibSession(RECT* changed){
 if(changed)return packDibRect(RECT{0,0,(LONG)image_.width,(LONG)image_.height},*changed);
 // The legacy publication leaves unquantized GDI output in the DIB.
 dib_generation_=0;
 const auto& format=image_.format;const DWORD bytes=pixel_bytes(format);const auto* input=static_cast<const DWORD*>(dc_bits_);const bool native_bgra=format.dwRGBBitCount==32&&format.dwRBitMask==0x00ff0000&&format.dwGBitMask==0x0000ff00&&format.dwBBitMask==0x000000ff&&format.dwRGBAlphaBitMask==0xff000000;if(native_bgra){const size_t row_bytes=(size_t)image_.width*4;for(UINT y=0;y<image_.height;++y)std::memcpy(image_.pixels.data()+(size_t)y*image_.pitch,input+(size_t)y*image_.width,row_bytes);}else for(UINT y=0;y<image_.height;++y)for(UINT x=0;x<image_.width;++x){const DWORD color=input[(size_t)y*image_.width+x];DWORD raw=pack_mask((BYTE)(color>>16),format.dwRBitMask)|pack_mask((BYTE)(color>>8),format.dwGBitMask)|pack_mask((BYTE)color,format.dwBBitMask)|pack_mask((BYTE)(color>>24),format.dwRGBAlphaBitMask);std::memcpy(image_.pixels.data()+(size_t)y*image_.pitch+(size_t)x*bytes,&raw,std::min<DWORD>(bytes,4));}
 return true;
}
namespace {
struct PixelChannel{DWORD mask=0,shift=0,maximum=0;};
PixelChannel pixel_channel(DWORD mask){PixelChannel c{mask,0,0};if(mask){while(((mask>>c.shift)&1)==0)++c.shift;c.maximum=mask>>c.shift;}return c;}
// Same arithmetic as pack_mask / expand_mask with the shifts hoisted.
DWORD pack_channel(BYTE value,const PixelChannel& c){return c.mask?((DWORD(value)*c.maximum+127)/255<<c.shift)&c.mask:0;}
BYTE expand_channel(DWORD raw,const PixelChannel& c,BYTE fallback){return c.mask&&c.maximum?(BYTE)((((raw&c.mask)>>c.shift)*255u+c.maximum/2)/c.maximum):fallback;}
}
// Writes back the DIB pixels inside `area` that differ from the surface and
// leaves the DIB equal to what a full unpack of the result would give.
bool Surface::packDibRect(const RECT& area,RECT& changed){
 changed={};
 const LONG x0=std::max<LONG>(0,area.left),y0=std::max<LONG>(0,area.top),x1=std::min<LONG>((LONG)image_.width,area.right),y1=std::min<LONG>((LONG)image_.height,area.bottom);
 if(x1<=x0||y1<=y0)return false;
 const auto& format=image_.format;const DWORD stride=pixel_bytes(format),bytes=std::min<DWORD>(stride,4);
 auto* dib=static_cast<DWORD*>(dc_bits_);
 const bool native_bgra=format.dwRGBBitCount==32&&format.dwRBitMask==0x00ff0000&&format.dwGBitMask==0x0000ff00&&format.dwBBitMask==0x000000ff&&format.dwRGBAlphaBitMask==0xff000000;
 const PixelChannel r=pixel_channel(format.dwRBitMask),g=pixel_channel(format.dwGBitMask),b=pixel_channel(format.dwBBitMask),a=pixel_channel(format.dwRGBAlphaBitMask);
 LONG left=x1,top=y1,right=x0,bottom=y0;
 BYTE* pixels=image_.pixels.data();
 for(LONG y=y0;y<y1;++y){
  BYTE* row=pixels+(size_t)y*image_.pitch;DWORD* source=dib+(size_t)y*image_.width;
  if(native_bgra){
   if(std::memcmp(row+(size_t)x0*4,source+x0,(size_t)(x1-x0)*4)==0)continue;
   LONG first=x0,last=x1-1;
   while(std::memcmp(row+(size_t)first*4,source+first,4)==0)++first;
   while(std::memcmp(row+(size_t)last*4,source+last,4)==0)--last;
   std::memcpy(row+(size_t)first*4,source+first,(size_t)(last-first+1)*4);
   left=std::min(left,first);right=std::max(right,last+1);top=std::min(top,y);bottom=std::max(bottom,y+1);
   continue;
  }
  for(LONG x=x0;x<x1;++x){
   const DWORD color=source[x];
   const DWORD raw=pack_channel((BYTE)(color>>16),r)|pack_channel((BYTE)(color>>8),g)|pack_channel((BYTE)color,b)|pack_channel((BYTE)(color>>24),a);
   source[x]=(DWORD(expand_channel(raw,a,255))<<24)|(DWORD(expand_channel(raw,r,0))<<16)|(DWORD(expand_channel(raw,g,0))<<8)|expand_channel(raw,b,0);
   BYTE* pixel=row+(size_t)x*stride;
   if(std::memcmp(pixel,&raw,bytes)==0)continue;
   std::memcpy(pixel,&raw,bytes);
   left=std::min(left,x);right=std::max(right,x+1);top=std::min(top,y);bottom=std::max(bottom,y+1);
  }
 }
 if(right<=left||bottom<=top)return false;
 changed={left,top,right,bottom};return true;
}
void Surface::finishLayerWrite(bool changed,const RECT& rect){
 image_.cpu_access_active=false;image_.gpu_captured=false;
 if(changed){mark_surface_dirty(image_,rect);advance_surface_generation(image_,&rect);++unique_;completedVisualMutation();}
 finishCpuWrite();
}
bool Surface::ownedLayerClaim(const void* caller){
 if(!owner_->renderer.owned_ui_text_enabled())return false;
 static bool attempted=false;
 if(!attempted){
  attempted=true;
  const char* reason=owned::game_sites_ok(owned::GameSiteGroup::ui_layers)?ui_layers_install(reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr))):"site_bytes_mismatch";
  bridge_append_probe_event(reason?"owned_ui_text_rejected":"owned_ui_text_installed",reason?reason:"import_slots");
 }
 return ui_layers_active()&&ui_layer_getdc_caller(caller);
}
HDC Surface::promoteLayer(void* context,const std::uint8_t* recipe,std::size_t size) noexcept{
 // A RichEdit range is a labelled legacy layer: it draws on the DIB.
 auto* surface=static_cast<Surface*>(context);
 if(FAILED(surface->beginDibSession()))return nullptr;
 if(!ui_layer_replay(recipe,size,surface->dc_)){surface->image_.cpu_access_active=false;return nullptr;}
 surface->layer_promoted_=true;return surface->dc_;
}
void Surface::omitLayer(UiLayerFailure failure){
 static std::uint32_t reported=0;const std::uint32_t bit=1u<<static_cast<unsigned>(failure);
 if(!(reported&bit)){reported|=bit;bridge_append_probe_event("owned_ui_text_omitted",ui_layer_failure_name(failure));}
 layer_state_={};
 if(FAILED(ensurePixelsForWrite(true))||FAILED(ensureDcStorage())){image_.cpu_access_active=false;return;}
 image_.cpu_access_active=true;dc_last_present_=g_present_serial;
 std::memset(dc_bits_,0,(size_t)image_.width*image_.height*4);
 // Diagnostic marker: an omitted layer shows only a magenta outline.
 const RECT whole{0,0,(LONG)image_.width,(LONG)image_.height};
 if(const HBRUSH brush=CreateSolidBrush(RGB(255,0,255))){::FrameRect(dc_,&whole,brush);DeleteObject(brush);}
 packDibSession(nullptr);finishLayerWrite(true,whole);
}
HRESULT Surface::releaseLayerDc(HDC value){
 if(value!=layer_dc_)return DDERR_NODC;
 const UiLayerEnd end=ui_layer_end(layer_dc_,layer_recipe_);
 const bool promoted=layer_promoted_;
 dc_active_=false;dc_layer_=false;layer_dc_=nullptr;layer_promoted_=false;
 if(end.failure!=UiLayerFailure::none){omitLayer(end.failure);return DD_OK;}
 if(promoted){
  static bool reported=false;if(!reported){reported=true;bridge_append_probe_event("owned_ui_text_legacy_layer","rich_edit");}
  RECT rect{};const bool changed=packDibSession(&rect);finishLayerWrite(changed,rect);dib_generation_=image_.generation;layer_state_={};return DD_OK;
 }
 // Same recipe on the pixels its last no-op replay left: nothing to do.
 if(!image_.gpu_captured&&!lost_&&ui_layer_unchanged(layer_state_,layer_recipe_,image_.generation))return DD_OK;
 // The DIB still holds this generation: replay on it and write back only
 // what the replay drew. Otherwise unpack the surface first, as legacy does.
 const bool current=dc_&&dib_generation_==image_.generation&&!image_.gpu_captured&&!image_.cpu_released&&!image_.pixels.empty();
 HRESULT hr=DD_OK;
 if(current){image_.cpu_access_active=true;dc_last_present_=g_present_serial;}else hr=beginDibSession();
 RECT touched{};
 if(FAILED(hr)||!ui_layer_replay(layer_recipe_.data(),layer_recipe_.size(),dc_,&touched)){image_.cpu_access_active=false;omitLayer(UiLayerFailure::allocation);return DD_OK;}
 RECT rect{};const bool changed=packDibRect(touched,rect);finishLayerWrite(changed,rect);dib_generation_=image_.generation;
 ui_layer_commit(layer_state_,layer_recipe_,image_.generation,changed);
 return DD_OK;
}
HRESULT Surface::Lock(LPRECT r,LPDDSURFACEDESC2 d,DWORD flags,HANDLE){
 trace("Surface.Lock");
 HudAtlasMutationWindow hud_window{};
 const bool hud_candidate=bridge_consume_hud_atlas_window(
  static_cast<IDirectDrawSurface7*>(this),_ReturnAddress(),hud_window);
 if(!d)return DDERR_INVALIDPARAMS;
 if(locked_)return DDERR_SURFACEBUSY;
 hud_atlas_snapshot_={};
 LONG x=r?r->left:0,y=r?r->top:0;
 if(x<0||y<0||x>=(LONG)image_.width||y>=(LONG)image_.height||
    (r&&(r->right<x||r->bottom<y||r->right>(LONG)image_.width||
         r->bottom>(LONG)image_.height)))return DDERR_INVALIDRECT;
 if(block_compressed(image_.format)&&((x|y)&3))return DDERR_INVALIDRECT;
 // A HUD atlas producer rewrites its atlas every frame: keep it CPU-resident
 // (an exact restore of a released atlas is authoritative CPU data).
 if(hud_candidate||(flags&DDLOCK_READONLY)){const HRESULT pinned=restoreReleased(hud_candidate);if(FAILED(pinned))return pinned;}
 // Observe eligibility before the ordinary Lock can read back or allocate.
 const bool cpu_authoritative_before_lock=
  hud_atlas_cpu_pixels_authoritative(image_);
 HRESULT hr=legacyCpuAccess();
 if(FAILED(hr))return hr;
 // Queued visual values may share these pixels. Detach before publishing a
 // writable Lock, and return its allocation failure without leaving it active.
 std::uint8_t* pixels=nullptr;
 try{pixels=image_.pixels.data();}catch(const std::bad_alloc&){return DDERR_OUTOFMEMORY;}
 if(image_.mip_source_used)image_.static_mip_hint=false;
 locked_=true;
 image_.cpu_access_active=true;
 locked_flags_=flags;
 locked_rect_=r?*r:RECT{0,0,(LONG)image_.width,(LONG)image_.height};
 if(hud_candidate)capture_hud_atlas_mutation(
  image_,hud_window,locked_rect_,flags,cpu_authoritative_before_lock,
  dc_active_,hud_atlas_snapshot_);
 *d=desc_;
 d->dwSize=sizeof(*d);
 d->lpSurface=block_compressed(image_.format)
  ?pixels+(size_t)(y/4)*image_.pitch+
   (size_t)(x/4)*block_bytes(image_.format)
  :pixels+(size_t)y*image_.pitch+
   (size_t)x*pixel_bytes(image_.format);
 return DD_OK;
}
HRESULT Surface::Unlock(LPRECT){
 trace("Surface.Unlock");
 if(!locked_)return DDERR_NOTLOCKED;
 bool increment_uniqueness=false;
 // Compare and account while this DirectDraw Lock epoch is still active.
 finish_hud_atlas_surface_write(
  image_,locked_flags_,locked_rect_,hud_atlas_snapshot_,increment_uniqueness);
 if(increment_uniqueness)++unique_;
 locked_flags_=0;
 locked_rect_={};
 locked_=false;
 image_.cpu_access_active=false;
 completedVisualMutation();
 // Upload the completed generation; draw consumption decides when it can drop.
 finishCpuWrite();
 return DD_OK;
}
HRESULT Surface::GetColorKey(DWORD,LPDDCOLORKEY k){if(!k)return DDERR_INVALIDPARAMS;if(!has_key_)return DDERR_NOCOLORKEYHW;*k=key_;return DD_OK;}
HRESULT Surface::SetColorKey(DWORD flags,LPDDCOLORKEY k){trace("Surface.SetColorKey","flags=%08lx low=%08lx high=%08lx",flags,k?k->dwColorSpaceLowValue:0,k?k->dwColorSpaceHighValue:0);mark_surface_dirty_full(image_);if(!k){has_key_=false;image_.has_color_key=false;advance_surface_generation(image_,nullptr);completedVisualMutation();return DD_OK;}key_=*k;has_key_=true;image_.color_key=*k;image_.has_color_key=true;advance_surface_generation(image_,nullptr);completedVisualMutation();return DD_OK;}
HRESULT Surface::GetDDInterface(LPVOID* out){if(!out)return DDERR_INVALIDPARAMS;*out=static_cast<IDirectDraw7*>(owner_);owner_->AddRef();return DD_OK;}

// Blit/Load sources are CPU composition inputs, even when also sampled by D3D.
// Pin their exact bytes before restoration; never drop them after a copy.
static HRESULT copy_rect(Surface& dst,LONG dx,LONG dy,Surface& src,const RECT* sr,bool publish_visual=true){
 RECT r=sr?*sr:RECT{0,0,(LONG)src.image()->width,(LONG)src.image()->height};
 if(r.left<0){dx-=r.left;r.left=0;}if(r.top<0){dy-=r.top;r.top=0;}if(dx<0){r.left-=dx;dx=0;}if(dy<0){r.top-=dy;dy=0;}
 LONG w=std::min({r.right-r.left,(LONG)src.image()->width-r.left,(LONG)dst.image()->width-dx});
 LONG h=std::min({r.bottom-r.top,(LONG)src.image()->height-r.top,(LONG)dst.image()->height-dy});
 const bool full_overwrite=&dst!=&src&&dx==0&&dy==0&&w==(LONG)dst.image()->width&&h==(LONG)dst.image()->height;
 HRESULT hr=src.restoreReleased(false);if(FAILED(hr))return hr;hr=src.legacyCpuAccess();if(FAILED(hr))return hr;
 if(w<=0||h<=0){return DD_OK;}
 // A whole-surface copy from an equal-layout source (the DX7 staging ->
 // texture load) takes the source bytes copy-on-write instead of allocating,
 // zero-filling and copying a destination buffer: the destination's bytes are
 // exactly the memmove result. A later write to either surface (Lock, DC,
 // copy) detaches its own copy, so DX7 Lock semantics stay exact. Neither
 // surface may hold an open Lock/DC pointer into its bytes.
 const auto share_whole_source=[&]{
  auto& d=*dst.image();const auto& s=*src.image();
  const size_t bytes=(size_t)d.pitch*surface_rows(d.height,d.format);
  if(!full_overwrite||r.left!=0||r.top!=0||s.width!=d.width||s.height!=d.height||s.pitch!=d.pitch||
     d.cpu_access_active||s.cpu_access_active||s.pixels.size()!=bytes)return false;
  d.pixels=s.pixels;d.cpu_released=false;update_surface_memory_accounting(d);
  return true;
 };
 if(block_compressed(dst.image()->format)||block_compressed(src.image()->format)){
  const auto& df=dst.image()->format;const auto& sf=src.image()->format;
  if(!block_compressed(df)||!block_compressed(sf)||df.dwFourCC!=sf.dwFourCC||((dx|dy|r.left|r.top)&3))return DDERR_INVALIDPIXELFORMAT;
  const bool shared=share_whole_source();
  if(!shared){hr=dst.ensurePixelsForWrite(full_overwrite);if(FAILED(hr))return hr;}
  const LONG bw=(w+3)/4,bh=(h+3)/4;const DWORD bytes=block_bytes(df);
  PostEngineSurfaceTimingScope timing(PostEngineSurfaceTimingKind::cpu_rect);
  if(!shared)for(LONG y=0;y<bh;y++)std::memmove(dst.image()->pixels.data()+(size_t)(dy/4+y)*dst.image()->pitch+(size_t)(dx/4)*bytes,std::as_const(src.image()->pixels).data()+(size_t)(r.top/4+y)*src.image()->pitch+(size_t)(r.left/4)*bytes,(size_t)bw*bytes);
  dst.image()->gpu_captured=false;{const RECT changed{dx,dy,dx+w,dy+h};mark_surface_dirty(*dst.image(),changed);advance_surface_generation(*dst.image(),&changed);}timing.finish((std::uint64_t)w*h);if(publish_visual)dst.completedVisualCpuWrite(dx==0&&dy==0&&w==(LONG)dst.image()->width&&h==(LONG)dst.image()->height,src.visualCpuCurrent());else dst.invalidateVisualCpu();return DD_OK;
 }
 DWORD db=pixel_bytes(dst.image()->format),sb=pixel_bytes(src.image()->format);if(db!=sb)return DDERR_INVALIDPIXELFORMAT;
 const bool shared=share_whole_source();
 if(!shared){hr=dst.ensurePixelsForWrite(full_overwrite);if(FAILED(hr))return hr;}
 PostEngineSurfaceTimingScope timing(PostEngineSurfaceTimingKind::cpu_rect);if(!shared)for(LONG y=0;y<h;y++)std::memmove(dst.image()->pixels.data()+(size_t)(dy+y)*dst.image()->pitch+(size_t)dx*db,std::as_const(src.image()->pixels).data()+(size_t)(r.top+y)*src.image()->pitch+(size_t)r.left*sb,(size_t)w*db);dst.image()->gpu_captured=false;{const RECT changed{dx,dy,dx+w,dy+h};mark_surface_dirty(*dst.image(),changed);advance_surface_generation(*dst.image(),&changed);}timing.finish((std::uint64_t)w*h);if(publish_visual)dst.completedVisualCpuWrite(dx==0&&dy==0&&w==(LONG)dst.image()->width&&h==(LONG)dst.image()->height,src.visualCpuCurrent());else dst.invalidateVisualCpu();return DD_OK;
}
static HRESULT stretch_rect(Surface& dst,const RECT& dr,Surface& src,const RECT& sr,bool publish_visual=true){
 const LONG dw=dr.right-dr.left,dh=dr.bottom-dr.top,sw=sr.right-sr.left,sh=sr.bottom-sr.top;if(dw<=0||dh<=0||sw<=0||sh<=0)return DDERR_INVALIDRECT;
 if(block_compressed(dst.image()->format)||block_compressed(src.image()->format))return DDERR_INVALIDPIXELFORMAT;
 HRESULT hr=dst.ensurePixels();if(FAILED(hr))return hr;hr=src.restoreReleased(false);if(FAILED(hr))return hr;hr=src.legacyCpuAccess();if(FAILED(hr))return hr;
 const DWORD db=pixel_bytes(dst.image()->format),sb=pixel_bytes(src.image()->format);if(db!=sb)return DDERR_INVALIDPIXELFORMAT;
 const RECT clipped{std::max<LONG>(0,dr.left),std::max<LONG>(0,dr.top),std::min<LONG>((LONG)dst.image()->width,dr.right),std::min<LONG>((LONG)dst.image()->height,dr.bottom)};if(clipped.right<=clipped.left||clipped.bottom<=clipped.top)return DD_OK;
 PostEngineSurfaceTimingScope timing(PostEngineSurfaceTimingKind::cpu_rect);for(LONG y=clipped.top;y<clipped.bottom;++y)for(LONG x=clipped.left;x<clipped.right;++x){const POINT source=capture_stretch_source_point(sr,dr,x,y);if(source.x<0||source.y<0||source.x>=(LONG)src.image()->width||source.y>=(LONG)src.image()->height)continue;std::memcpy(dst.image()->pixels.data()+(size_t)y*dst.image()->pitch+(size_t)x*db,std::as_const(src.image()->pixels).data()+(size_t)source.y*src.image()->pitch+(size_t)source.x*sb,db);}
 dst.image()->gpu_captured=false;mark_surface_dirty(*dst.image(),clipped);advance_surface_generation(*dst.image(),&clipped);timing.finish((std::uint64_t)(clipped.right-clipped.left)*(clipped.bottom-clipped.top));if(publish_visual)dst.completedVisualCpuWrite(clipped.left==0&&clipped.top==0&&clipped.right==(LONG)dst.image()->width&&clipped.bottom==(LONG)dst.image()->height&&sr.left>=0&&sr.top>=0&&sr.right<=(LONG)src.image()->width&&sr.bottom<=(LONG)src.image()->height,src.visualCpuCurrent());else dst.invalidateVisualCpu();return DD_OK;
}
HRESULT Surface::BltFast(DWORD x,DWORD y,LPDIRECTDRAWSURFACE7 src,LPRECT r,DWORD flags){
 PostEngineSurfaceTimingScope proxy_timing(PostEngineSurfaceTimingKind::proxy);
 auto* s=dynamic_cast<Surface*>(src);trace("Surface.BltFast","target_primary=%d target=%ux%u source=%ux%u active_target=%d",primary()?1:0,image_.width,image_.height,s?s->image()->width:0,s?s->image()->height:0,s==owner_->active_target?1:0);if(!s)return DDERR_INVALIDOBJECT;
 if(owner_->active_device&&(this==owner_->active_target||primary()||s==owner_->active_target)){HRESULT flush=owner_->active_device->flushForSurface();if(FAILED(flush))return owner_->checkDevice(flush);}
 if(primary()&&s==owner_->active_target){invalidateVisualCpu();proxy_timing.finish();return owner_->presentVisual();}
 RECT sourceRect=r?*r:RECT{0,0,(LONG)s->image()->width,(LONG)s->image()->height};RECT destination{(LONG)x,(LONG)y,(LONG)x+(sourceRect.right-sourceRect.left),(LONG)y+(sourceRect.bottom-sourceRect.top)};const DDCOLORKEY* key=(flags&DDBLTFAST_SRCCOLORKEY)&&s->image()->has_color_key?&s->image()->color_key:nullptr;
 if(!((this==owner_->active_target||primary())&&released_surface_gpu_blit(*s->image(),key))){const HRESULT restored=s->restoreReleased(false);if(FAILED(restored))return restored;}
 if(this==owner_->active_target||primary()){
  invalidateVisualTarget();HRESULT hr=owner_->renderer.blit(*s->image(),destination,&sourceRect,key,this==owner_->active_target);
  if(SUCCEEDED(hr)&&primary()){proxy_timing.finish();return owner_->presentVisual();}
  return owner_->checkDevice(hr);
 }
 return copy_rect(*this,(LONG)x,(LONG)y,*s,r);
}
HRESULT Surface::Blt(LPRECT dr,LPDIRECTDRAWSURFACE7 src,LPRECT sr,DWORD flags,LPDDBLTFX fx){
 PostEngineSurfaceTimingScope proxy_timing(PostEngineSurfaceTimingKind::proxy);
 camp::OutputScope output(compositionOutput());
 RECT requested=dr?*dr:RECT{0,0,(LONG)image_.width,(LONG)image_.height};trace("Surface.Blt","flags=%08lx rect=%ld,%ld,%ld,%ld target=%ux%u",flags,requested.left,requested.top,requested.right,requested.bottom,image_.width,image_.height);
 if(flags&(DDBLT_COLORFILL|DDBLT_DEPTHFILL)){if(!fx)return DDERR_INVALIDPARAMS;DWORD c=(flags&DDBLT_DEPTHFILL)?fx->dwFillDepth:fx->dwFillColor;RECT r{std::max<LONG>(0,requested.left),std::max<LONG>(0,requested.top),std::min<LONG>((LONG)image_.width,requested.right),std::min<LONG>((LONG)image_.height,requested.bottom)};if(r.right<=r.left||r.bottom<=r.top)return DDERR_INVALIDRECT;HRESULT hr=ensurePixelsForWrite(r.left==0&&r.top==0&&r.right==(LONG)image_.width&&r.bottom==(LONG)image_.height);if(FAILED(hr))return hr;DWORD bytes=pixel_bytes(image_.format);PostEngineSurfaceTimingScope rect_timing(PostEngineSurfaceTimingKind::cpu_rect);for(LONG y=r.top;y<r.bottom;y++){auto* row=image_.pixels.data()+(size_t)y*image_.pitch+(size_t)r.left*bytes;for(LONG x=r.left;x<r.right;x++,row+=bytes)std::memcpy(row,&c,bytes);}mark_surface_dirty(image_,r);advance_surface_generation(image_,&r);rect_timing.finish((std::uint64_t)(r.right-r.left)*(r.bottom-r.top));completedVisualCpuWrite(r.left==0&&r.top==0&&r.right==(LONG)image_.width&&r.bottom==(LONG)image_.height,true);if(primary()){proxy_timing.finish();hr=owner_->presentVisual();}return hr;}
 auto* s=dynamic_cast<Surface*>(src);if(!s)return DDERR_INVALIDOBJECT;trace("Surface.Blt.Source","id=%llu %ux%u flags=%08lx fourcc=%08lx bpp=%lu active_target=%d",(unsigned long long)s->image()->id,s->image()->width,s->image()->height,s->image()->format.dwFlags,s->image()->format.dwFourCC,s->image()->format.dwRGBBitCount,s==owner_->active_target?1:0);if(state_trace_enabled()){RECT sourceRect=sr?*sr:RECT{0,0,(LONG)s->image()->width,(LONG)s->image()->height};char event[320]{};sprintf_s(event,"Diagnostic.Blt.t%llu.tp%d.s%llu.sa%d.f%08lx.d%ld.%ld.%ld.%ld.r%ld.%ld.%ld.%ld",(unsigned long long)image_.id,primary()?1:0,(unsigned long long)s->image()->id,s==owner_->active_target?1:0,flags,requested.left,requested.top,requested.right,requested.bottom,sourceRect.left,sourceRect.top,sourceRect.right,sourceRect.bottom);trace(event);}
 if(owner_->active_device&&(this==owner_->active_target||primary()||s==owner_->active_target)){HRESULT flush=owner_->active_device->flushForSurface();if(FAILED(flush))return owner_->checkDevice(flush);}
 if(primary()&&s==owner_->active_target){invalidateVisualCpu();proxy_timing.finish();return owner_->presentVisual();}
 HRESULT owned_text_result=DD_OK;if(bridge_consume_character_pane_blt(s->image()->id,_ReturnAddress(),requested,sr,owner_->renderer,owned_text_result,s->image()->generation)){invalidateVisualTarget();return owned_text_result;}
 DDCOLORKEY overrideKey{};const DDCOLORKEY* key=nullptr;if(flags&DDBLT_KEYSRCOVERRIDE){if(fx)overrideKey=fx->ddckSrcColorkey;key=&overrideKey;}else if((flags&DDBLT_KEYSRC)&&s->image()->has_color_key)key=&s->image()->color_key;
 if(!((this==owner_->active_target||primary())&&released_surface_gpu_blit(*s->image(),key))){const HRESULT restored=s->restoreReleased(false);if(FAILED(restored))return restored;}
 const auto destinationArea=(std::uint64_t)std::max<LONG>(0,requested.right-requested.left)*std::max<LONG>(0,requested.bottom-requested.top),primaryArea=(std::uint64_t)image_.width*image_.height;// A picture blitted over the whole screen (loading screens, movies) is shown at its own aspect; one the game placed itself (the 1920x816 movies at 3840x1632) keeps its rectangle. Either way the rest of the screen gets the pillarbox fill, never the world composite behind it (issue 172).
 const bool covers_screen=image_.width==owner_->width&&image_.height==owner_->height&&requested.left<=0&&requested.top<=0&&requested.right>=(LONG)image_.width&&requested.bottom>=(LONG)image_.height;
 if(primary()&&destinationArea>primaryArea/4){owner_->renderer.mark_load_picture();note_large_primary_picture(*s->image());trace("Surface.Blt.LargeCpuPresented","source=%ux%u destination=%ldx%ld",s->image()->width,s->image()->height,requested.right-requested.left,requested.bottom-requested.top);invalidateVisualTarget();HRESULT hr=owner_->renderer.present_image(*s->image(),sr,key,covers_screen?nullptr:&requested);if(SUCCEEDED(hr)){proxy_timing.finish();return owner_->presentVisual();}return owner_->checkDevice(hr);}
 RECT sourceRect=sr?*sr:RECT{0,0,(LONG)s->image()->width,(LONG)s->image()->height};const bool stretched=requested.right-requested.left!=sourceRect.right-sourceRect.left||requested.bottom-requested.top!=sourceRect.bottom-sourceRect.top;const bool publish_cpu=this!=owner_->active_target&&!primary();if(s==owner_->active_target&&this!=owner_->active_target){invalidateVisualCpu();if(owner_->renderer.has_pending_ui())owner_->invalidateVisualOutput();}const bool target_capture=s==owner_->active_target&&this!=owner_->active_target;
 if(target_capture){const HRESULT restored=restoreReleased();if(FAILED(restored))return restored;}
 // The active target and primary are GPU-owned: the renderer blit below is
 // their only real write. Their CPU mirror cannot hold the GPU-rendered world,
 // so copying UI pixels into it only cost CPU (~1-3 GB/s in the store screen).
 HRESULT hr=target_capture?owner_->renderer.capture(image_,requested,sourceRect):
  !publish_cpu?DD_OK:(stretched?stretch_rect(*this,requested,*s,sourceRect,publish_cpu):copy_rect(*this,requested.left,requested.top,*s,sr,publish_cpu));
 if(SUCCEEDED(hr)&&this==owner_->active_target){if(key){trace("Surface.Blt.ActiveTargetKeyed","key=%08lx-%08lx source=%ld,%ld,%ld,%ld",key->dwColorSpaceLowValue,key->dwColorSpaceHighValue,sourceRect.left,sourceRect.top,sourceRect.right,sourceRect.bottom);}invalidateVisualTarget();hr=covers_screen&&!key?owner_->renderer.present_image(*s->image(),sr,key):owner_->renderer.blit(*s->image(),requested,sr,key,true);}if(SUCCEEDED(hr)&&primary()){if(key){trace("Surface.Blt.Keyed","key=%08lx-%08lx source=%ld,%ld,%ld,%ld",key->dwColorSpaceLowValue,key->dwColorSpaceHighValue,sourceRect.left,sourceRect.top,sourceRect.right,sourceRect.bottom);}invalidateVisualTarget();hr=owner_->renderer.blit(*s->image(),requested,sr,key);}if(SUCCEEDED(hr))completedVisualMutation();return owner_->checkDevice(hr);
}
HRESULT Surface::Flip(LPDIRECTDRAWSURFACE7,DWORD){trace("Surface.Flip");return owner_->presentVisual();}
HRESULT Device::Load(LPDIRECTDRAWSURFACE7 destination,LPPOINT point,LPDIRECTDRAWSURFACE7 source,LPRECT sourceRect,DWORD flags){trace("Device.Load","flags=%08lx",flags);auto* dst=dynamic_cast<Surface*>(destination);auto* src=dynamic_cast<Surface*>(source);if(!dst||!src){trace("Device.Load.Result","hr=%08lx invalid-object",DDERR_INVALIDOBJECT);return DDERR_INVALIDOBJECT;}if(scene_){HRESULT flush=flushForSurface();if(FAILED(flush))return flush;}const LONG x=point?point->x:0,y=point?point->y:0;HRESULT hr=copy_rect(*dst,x,y,*src,sourceRect);
 if(SUCCEEDED(hr)){
  DDSCAPS2 caps{};dst->GetCaps(&caps);
  const auto& image=*dst->image();const auto& source_image=*src->image();
  const bool full_copy=x==0&&y==0&&image.width==source_image.width&&image.height==source_image.height&&
   (!sourceRect||(sourceRect->left==0&&sourceRect->top==0&&sourceRect->right==(LONG)source_image.width&&sourceRect->bottom==(LONG)source_image.height));
  // Device construction can leave ensure() unsuccessful. Keep CPU publication
  // pending in that lifetime; Load must not initialize a renderer on its own.
  if((caps.dwCaps&DDSCAPS_TEXTURE)&&full_copy&&dd_->renderer.ready()){
   hr=dd_->renderer.upload(*dst->image());
  }
 }
 trace("Device.Load.Result","hr=%08lx dst=%ux%u/%lu src=%ux%u/%lu point=%ld,%ld",hr,dst->image()->width,dst->image()->height,dst->image()->format.dwRGBBitCount,src->image()->width,src->image()->height,src->image()->format.dwRGBBitCount,x,y);if(state_trace_enabled()){RECT r=sourceRect?*sourceRect:RECT{0,0,(LONG)src->image()->width,(LONG)src->image()->height};char event[320]{};sprintf_s(event,"Diagnostic.Load.d%llu.s%llu.hr%08lx.p%ld.%ld.r%ld.%ld.%ld.%ld.dg%llu.sg%llu",(unsigned long long)dst->image()->id,(unsigned long long)src->image()->id,hr,x,y,r.left,r.top,r.right,r.bottom,(unsigned long long)dst->image()->generation,(unsigned long long)src->image()->generation);trace(event);}return dd_->checkDevice(hr);}

namespace {
DWORD WINAPI device_removed_message(LPVOID) {
 MessageBoxW(nullptr,
  L"The graphics driver stopped responding (GPU timeout). Lower graphics settings (F10) or update the driver.",
  L"Evil Islands",MB_OK|MB_ICONERROR|MB_TOPMOST|MB_SETFOREGROUND);
 return 0;
}
}  // namespace

void DD::deviceRemovedExit(HRESULT reason) noexcept {
 renderer.report_device_removed(reason);
 ClipCursor(nullptr);
 // The message runs on its own thread: the game thread waits without pumping
 // window messages, so no game or renderer code runs again before the end.
 // That thread also has its own cursor state, visible over the message.
 if(const HANDLE thread=CreateThread(nullptr,0,&device_removed_message,nullptr,0,nullptr)){
  WaitForSingleObject(thread,INFINITE);
  CloseHandle(thread);
 } else device_removed_message(nullptr);
 // No exit(): the game's CRT destructors are the crash this avoids.
 TerminateProcess(GetCurrentProcess(),static_cast<UINT>(reason));
 ExitProcess(static_cast<UINT>(reason));
}

HRESULT DD::CreateSurface(LPDDSURFACEDESC2 d,LPDIRECTDRAWSURFACE7* out,IUnknown* outer){trace("DD.CreateSurface");if(!d||!out||outer)return DDERR_INVALIDPARAMS;*out=nullptr;auto* s=new(std::nothrow)Surface(this,*d);if(!s)return DDERR_OUTOFMEMORY;*out=s;if((d->ddsCaps.dwCaps&DDSCAPS_COMPLEX)&&(d->ddsCaps.dwCaps&DDSCAPS_FLIP)&&d->dwBackBufferCount){DDSURFACEDESC2 bd=*d;bd.ddsCaps.dwCaps&=~DDSCAPS_PRIMARYSURFACE;bd.ddsCaps.dwCaps|=DDSCAPS_BACKBUFFER;bd.dwBackBufferCount=0;auto* back=new(std::nothrow)Surface(this,bd);if(back){s->AddAttachedSurface(back);back->Release();}}return DD_OK;}
HRESULT DD::DuplicateSurface(LPDIRECTDRAWSURFACE7 src,LPDIRECTDRAWSURFACE7* out){trace("DD.DuplicateSurface");auto* s=dynamic_cast<Surface*>(src);if(!s||!out)return DDERR_INVALIDPARAMS;DDSURFACEDESC2 d{};d.dwSize=sizeof(d);s->GetSurfaceDesc(&d);return CreateSurface(&d,out,nullptr);}
HRESULT DD::EnumDisplayModes(DWORD,LPDDSURFACEDESC2 filter,LPVOID c,LPDDENUMMODESCALLBACK2 cb){trace("DD.EnumDisplayModes");if(!cb)return DDERR_INVALIDPARAMS;const SIZE modes[]={{800,600},{1024,768},{1280,720},{1920,1080},{3840,2160}};for(auto m:modes){DDSURFACEDESC2 d{};d.dwSize=sizeof(d);d.dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|DDSD_REFRESHRATE;d.dwWidth=m.cx;d.dwHeight=m.cy;d.dwRefreshRate=60;d.ddpfPixelFormat=bgra32();if(filter&&filter->dwWidth&&filter->dwWidth!=d.dwWidth)continue;if(cb(&d,c)==DDENUMRET_CANCEL)break;}return DD_OK;}
HRESULT DD::GetCaps(LPDDCAPS d,LPDDCAPS h){
 trace("DD.GetCaps");auto fill=[](LPDDCAPS c){if(!c)return;DWORD requested=c->dwSize;std::memset(c,0,std::min<size_t>(requested,sizeof(*c)));c->dwSize=requested;
  c->dwCaps=DDCAPS_3D|DDCAPS_BLT|DDCAPS_BLTSTRETCH|DDCAPS_COLORKEY|DDCAPS_GDI|DDCAPS_PALETTE|DDCAPS_READSCANLINE|DDCAPS_ZBLTS|DDCAPS_BLTCOLORFILL|DDCAPS_BLTDEPTHFILL|DDCAPS_CANBLTSYSMEM;
  c->dwCaps2=DDCAPS2_CANRENDERWINDOWED;c->dwVidMemTotal=512u<<20;c->dwVidMemFree=384u<<20;
  c->ddsCaps.dwCaps=DDSCAPS_3DDEVICE|DDSCAPS_BACKBUFFER|DDSCAPS_COMPLEX|DDSCAPS_FLIP|DDSCAPS_FRONTBUFFER|DDSCAPS_OFFSCREENPLAIN|DDSCAPS_PRIMARYSURFACE|DDSCAPS_SYSTEMMEMORY|DDSCAPS_TEXTURE|DDSCAPS_VIDEOMEMORY|DDSCAPS_LOCALVIDMEM|DDSCAPS_ZBUFFER;
 };fill(d);fill(h);return DD_OK;
}
HRESULT DD::GetDisplayMode(LPDDSURFACEDESC2 d){if(!d)return DDERR_INVALIDPARAMS;DWORD size=d->dwSize;std::memset(d,0,sizeof(*d));d->dwSize=size?size:sizeof(*d);d->dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|DDSD_REFRESHRATE;d->dwWidth=width;d->dwHeight=height;d->dwRefreshRate=60;d->ddpfPixelFormat=bgra32();return DD_OK;}
HRESULT DD::GetGDISurface(LPDIRECTDRAWSURFACE7* out){if(!out)return DDERR_INVALIDPARAMS;if(!primary)return DDERR_NOTFOUND;*out=primary;primary->AddRef();return DD_OK;}
HRESULT DD::GetDeviceIdentifier(LPDDDEVICEIDENTIFIER2 id,DWORD){if(!id)return DDERR_INVALIDPARAMS;std::memset(id,0,sizeof(*id));strcpy_s(id->szDriver,"owned-d3d11");strcpy_s(id->szDescription,"Owned Evil Islands D3D11 prototype");id->guidDeviceIdentifier=IID_IDirect3DHALDevice;return DD_OK;}

HRESULT DD::EnumDevices(LPD3DENUMDEVICESCALLBACK7 cb,LPVOID c){trace("D3D7.EnumDevices");if(!cb)return DDERR_INVALIDPARAMS;D3DDEVICEDESC7 d{};fill_d3d_caps(d);char desc[]="Owned D3D11 translator",name[]="owned-d3d11";cb(desc,name,&d,c);return D3D_OK;}
// DirectX 7 device creation puts the calling thread's x87 unit in single
// precision unless the application asked for DDSCL_FPUPRESERVE. The game
// requests DDSCL_FPUSETUP and its own x87 math depends on that 24-bit
// precision. Only the precision field changes; rounding and exception masks
// stay as the application set them. Proxy code is compiled for SSE2.
static void apply_d3d7_fpu_precision(DWORD cooperative_flags) noexcept {
 if(cooperative_flags&DDSCL_FPUPRESERVE)return;
 unsigned int before=0,after=0;
 if(_controlfp_s(&before,0,0)!=0)return;
 if(_controlfp_s(&after,_PC_24,_MCW_PC)!=0)return;
 char detail[96];
 std::snprintf(detail,sizeof(detail),"coop=%08lx control_before=%08x control_after=%08x",
  static_cast<unsigned long>(cooperative_flags),before,after);
 bridge_append_probe_event("d3d7_fpu_single_precision",detail);
}

HRESULT DD::CreateDevice(REFCLSID cls,LPDIRECTDRAWSURFACE7 surface,LPDIRECT3DDEVICE7* out){trace("D3D7.CreateDevice");if(!out)return DDERR_INVALIDPARAMS;*out=nullptr;if(cls!=IID_IDirect3DHALDevice&&cls!=IID_IDirect3DTnLHalDevice&&cls!=IID_IDirect3DRGBDevice)return DDERR_NODIRECTDRAWHW;auto* s=dynamic_cast<Surface*>(surface);if(!s)return DDERR_INVALIDPARAMS;apply_d3d7_fpu_precision(coop);auto* dev=new(std::nothrow)Device(this,s);if(!dev)return E_OUTOFMEMORY;*out=dev;return D3D_OK;}
HRESULT DD::EnumZBufferFormats(REFCLSID,LPD3DENUMPIXELFORMATSCALLBACK cb,LPVOID c){trace("D3D7.EnumZBufferFormats");if(!cb)return DDERR_INVALIDPARAMS;auto f=z24s8();cb(&f,c);return D3D_OK;}

HRESULT Device::GetCaps(LPD3DDEVICEDESC7 d){if(!d)return DDERR_INVALIDPARAMS;fill_d3d_caps(*d);return D3D_OK;}
HRESULT Device::EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK cb,LPVOID c){trace("Device.EnumTextureFormats");if(!cb)return DDERR_INVALIDPARAMS;DDPIXELFORMAT formats[]={raw_format(DDPF_RGB|DDPF_PALETTEINDEXED8,8,0,0,0),rgb_format(16,0x7c00,0x03e0,0x001f),rgb_format(16,0x7c00,0x03e0,0x001f,0x8000),rgb_format(16,0x0f00,0x00f0,0x000f,0xf000),rgb_format(16,0xf800,0x07e0,0x001f),rgb_format(32,0x00ff0000,0x0000ff00,0x000000ff),bgra32(),raw_format(DDPF_BUMPDUDV,16,0x000000ff,0x0000ff00,0),raw_format(DDPF_BUMPDUDV|DDPF_BUMPLUMINANCE,16,0x0000001f,0x000003e0,0x0000fc00),raw_format(DDPF_BUMPDUDV|DDPF_BUMPLUMINANCE,32,0x000000ff,0x0000ff00,0x00ff0000),fourcc_format(MAKEFOURCC('D','X','T','1')),fourcc_format(MAKEFOURCC('D','X','T','2')),fourcc_format(MAKEFOURCC('D','X','T','3')),fourcc_format(MAKEFOURCC('D','X','T','4')),fourcc_format(MAKEFOURCC('D','X','T','5'))};for(auto& f:formats)if(cb(&f,c)==D3DENUMRET_CANCEL)break;return D3D_OK;}
HRESULT Device::SetRenderTarget(LPDIRECTDRAWSURFACE7 s,DWORD){trace("Device.SetRenderTarget");auto* p=dynamic_cast<Surface*>(s);if(!p)return DDERR_INVALIDPARAMS;if(scene_){HRESULT hr=flushForSurface();if(FAILED(hr))return hr;}if(target_==p){dd_->active_target=target_;return D3D_OK;}p->AddRef();auto* previous=target_;target_=p;dd_->active_target=target_;if(previous)previous->Release();return D3D_OK;}
HRESULT Device::GetRenderTarget(LPDIRECTDRAWSURFACE7* out){if(!out)return DDERR_INVALIDPARAMS;*out=target_;if(target_)target_->AddRef();return target_?D3D_OK:DDERR_INVALIDPARAMS;}
HRESULT Device::Clear(DWORD count,LPD3DRECT rects,DWORD flags,D3DCOLOR c,D3DVALUE z,DWORD s){camp::OutputScope output(target_&&target_->compositionOutput());bridge_profile_api_entry("Clear",_ReturnAddress());trace("Device.Clear","flags=%08lx rects=%lu",flags,count);if(scene_){HRESULT flush=flushForSurface();if(FAILED(flush))return dd_->checkDevice(flush);}if(flags&D3DCLEAR_TARGET)invalidateVisualTarget();return dd_->checkDevice(dd_->renderer.clear(count,rects,flags,c,z,s));}
HRESULT Device::SetTransform(D3DTRANSFORMSTATETYPE t,LPD3DMATRIX m){bridge_profile_api_entry("Transform",_ReturnAddress());trace("Device.SetTransform","state=%lu",(DWORD)t);if(!m||t>D3DTRANSFORMSTATE_PROJECTION)return DDERR_INVALIDPARAMS;if(recording_){recording_state_.transform[t]=*m;recording_transform_mask_[t]=true;}else state_.transform[t]=*m;return D3D_OK;}
HRESULT Device::GetTransform(D3DTRANSFORMSTATETYPE t,LPD3DMATRIX m){if(!m||t>D3DTRANSFORMSTATE_PROJECTION)return DDERR_INVALIDPARAMS;*m=state_.transform[t];return D3D_OK;}
HRESULT Device::SetViewport(LPD3DVIEWPORT7 v){bridge_profile_api_entry("Viewport",_ReturnAddress());trace("Device.SetViewport");if(!v)return DDERR_INVALIDPARAMS;if(recording_){recording_state_.viewport=*v;recording_viewport_mask_=true;}else state_.viewport=*v;return D3D_OK;}
HRESULT Device::GetViewport(LPD3DVIEWPORT7 v){if(!v)return DDERR_INVALIDPARAMS;*v=state_.viewport;return D3D_OK;}
HRESULT Device::SetMaterial(LPD3DMATERIAL7 m){bridge_profile_api_entry("Material",_ReturnAddress());trace("Device.SetMaterial");if(!m)return DDERR_INVALIDPARAMS;if(recording_){recording_state_.material=*m;recording_material_mask_=true;}else state_.material=*m;return D3D_OK;}
HRESULT Device::GetMaterial(LPD3DMATERIAL7 m){if(!m)return DDERR_INVALIDPARAMS;*m=state_.material;return D3D_OK;}
HRESULT Device::SetRenderState(D3DRENDERSTATETYPE s,DWORD v){trace("Device.SetRenderState","state=%lu value=%08lx",(DWORD)s,v);if(diagnostic_trace_enabled()){char event[96]{};sprintf_s(event,"Diagnostic.RenderState.%lu.%08lx",(DWORD)s,v);trace(event);}if((DWORD)s>=state_.render.size())return DDERR_INVALIDPARAMS;if(recording_){recording_state_.render[s]=v;recording_render_mask_[s]=true;}else state_.render[s]=v;return D3D_OK;}
HRESULT Device::GetRenderState(D3DRENDERSTATETYPE s,LPDWORD v){if(!v||(DWORD)s>=state_.render.size())return DDERR_INVALIDPARAMS;*v=state_.render[s];return D3D_OK;}
HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE t,DWORD fvf,LPVOID v,DWORD n,DWORD){camp::OutputScope output(target_&&target_->compositionOutput());trace("Device.DrawPrimitive","type=%lu fvf=%08lx vertices=%lu",(DWORD)t,fvf,n);if(state_trace_enabled()){auto* tx=textures_[0]?textures_[0]->image():nullptr;char event[448]{};sprintf_s(event,"Diagnostic.DrawState.p%lu.f%lx.c%lu.z%lu.zw%lu.b%lu.s%lu.d%lu.at%lu.af%lu.ar%lu.co%lu.ca1%lu.ca2%lu.ao%lu.aa1%lu.aa2%lu.ti%llu.tg%llu.tu%llu.tw%u.th%u.tf%08lx.tc%08lx.tb%lu",(DWORD)t,fvf,state_.render[D3DRENDERSTATE_CULLMODE],state_.render[D3DRENDERSTATE_ZENABLE],state_.render[D3DRENDERSTATE_ZWRITEENABLE],state_.render[D3DRENDERSTATE_ALPHABLENDENABLE],state_.render[D3DRENDERSTATE_SRCBLEND],state_.render[D3DRENDERSTATE_DESTBLEND],state_.render[D3DRENDERSTATE_ALPHATESTENABLE],state_.render[D3DRENDERSTATE_ALPHAFUNC],state_.render[D3DRENDERSTATE_ALPHAREF],state_.stage[0][D3DTSS_COLOROP],state_.stage[0][D3DTSS_COLORARG1],state_.stage[0][D3DTSS_COLORARG2],state_.stage[0][D3DTSS_ALPHAOP],state_.stage[0][D3DTSS_ALPHAARG1],state_.stage[0][D3DTSS_ALPHAARG2],(unsigned long long)(tx?tx->id:0),(unsigned long long)(tx?tx->generation:0),(unsigned long long)(tx?tx->uploaded_generation:0),tx?tx->width:0,tx?tx->height:0,tx?tx->format.dwFlags:0,tx?tx->format.dwFourCC:0,tx?tx->format.dwRGBBitCount:0);trace(event);}invalidateVisualTarget();return scene_?queueDraw(t,fvf,v,n,nullptr,0):dd_->renderer.draw(t,fvf,v,n,nullptr,0,state_,textures_[0]?textures_[0]->image():nullptr);}
HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE t,DWORD fvf,LPVOID v,DWORD n,LPWORD i,DWORD ic,DWORD){camp::OutputScope output(target_&&target_->compositionOutput());trace("Device.DrawIndexedPrimitive","type=%lu fvf=%08lx vertices=%lu indices=%lu",(DWORD)t,fvf,n,ic);if(state_trace_enabled()&&i&&ic){WORD maximum=0;for(DWORD x=0;x<ic;++x)maximum=std::max(maximum,i[x]);char range[160]{};sprintf_s(range,"Diagnostic.IndexRange.p%lu.f%lx.n%lu.ic%lu.max%u.oob%d",(DWORD)t,fvf,n,ic,maximum,maximum>=n?1:0);trace(range);}if(state_trace_enabled()){auto* tx=textures_[0]?textures_[0]->image():nullptr;char event[448]{};sprintf_s(event,"Diagnostic.DrawState.p%lu.f%lx.c%lu.z%lu.zw%lu.b%lu.s%lu.d%lu.at%lu.af%lu.ar%lu.co%lu.ca1%lu.ca2%lu.ao%lu.aa1%lu.aa2%lu.ti%llu.tg%llu.tu%llu.tw%u.th%u.tf%08lx.tc%08lx.tb%lu",(DWORD)t,fvf,state_.render[D3DRENDERSTATE_CULLMODE],state_.render[D3DRENDERSTATE_ZENABLE],state_.render[D3DRENDERSTATE_ZWRITEENABLE],state_.render[D3DRENDERSTATE_ALPHABLENDENABLE],state_.render[D3DRENDERSTATE_SRCBLEND],state_.render[D3DRENDERSTATE_DESTBLEND],state_.render[D3DRENDERSTATE_ALPHATESTENABLE],state_.render[D3DRENDERSTATE_ALPHAFUNC],state_.render[D3DRENDERSTATE_ALPHAREF],state_.stage[0][D3DTSS_COLOROP],state_.stage[0][D3DTSS_COLORARG1],state_.stage[0][D3DTSS_COLORARG2],state_.stage[0][D3DTSS_ALPHAOP],state_.stage[0][D3DTSS_ALPHAARG1],state_.stage[0][D3DTSS_ALPHAARG2],(unsigned long long)(tx?tx->id:0),(unsigned long long)(tx?tx->generation:0),(unsigned long long)(tx?tx->uploaded_generation:0),tx?tx->width:0,tx?tx->height:0,tx?tx->format.dwFlags:0,tx?tx->format.dwFourCC:0,tx?tx->format.dwRGBBitCount:0);trace(event);}invalidateVisualTarget();return scene_?queueDraw(t,fvf,v,n,i,ic):dd_->renderer.draw(t,fvf,v,n,i,ic,state_,textures_[0]?textures_[0]->image():nullptr);}

HRESULT Device::queueDraw(D3DPRIMITIVETYPE type,DWORD fvf,const void* vertices,DWORD vertexCount,const WORD* indices,DWORD indexCount){
 SceneQueueTimingScope sceneTiming;
 bridge_profile_api_entry(indexCount ? "DrawIndexed" : "Draw", nullptr);
 if(!vertices||!vertexCount||(indexCount&&!indices))return DDERR_INVALIDPARAMS;const UINT stride=legacy_fvf_stride(fvf);if(!stride)return E_NOTIMPL;
 HRESULT nativeFlush=flushNativeVisual();native_sequence_=false;if(FAILED(nativeFlush))return nativeFlush;
 // The capture counters are profiler state (scene thread in scene mode).
 const bool profileCapture=!dd_->renderer.scene_mode()&&dd_->renderer.capture_profiling_enabled();LARGE_INTEGER captureStart{};if(profileCapture)QueryPerformanceCounter(&captureStart);const std::uint64_t captureBytes=(std::uint64_t)stride*vertexCount+(std::uint64_t)sizeof(WORD)*indexCount;
 HRESULT uiFlush=dd_->flushVisualUi();if(FAILED(uiFlush))return uiFlush;
 const auto copy_vertices=[&](std::uint8_t* destination,size_t bytes){std::memcpy(destination,vertices,bytes);};
 const auto semantic=current_semantic_context();const auto hud=widescreen::interface_owned?widescreen::hud_blit:widescreen::HudComposition{};Surface* held=textures_[0];SurfaceImage* image=held?held->image():nullptr;
 try{
  if(!pending_draws_.empty()){
   auto& batch=pending_draws_.back();const bool transformed=(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW;const bool placed=camp::composing&&target_&&target_->compositionOutput();const bool compatible=batch.camp_placement_active==placed&&(!placed||batch.camp_placement==camp::visual)&&type==D3DPT_TRIANGLELIST&&batch.type==D3DPT_TRIANGLELIST&&batch.fvf==fvf&&batch.texture==image&&batch.semantic_kind==semantic.kind&&batch.caller_rva==semantic.caller_rva&&widescreen::same_composition(batch.hud_composition,hud)&&(batch.index_count==0)==(indexCount==0)&&batch.vertex_count+vertexCount<=0xffffu&&batch.vertex_offset+(size_t)stride*batch.vertex_count==pending_vertex_bytes_.size()&&(!batch.index_count||batch.index_offset+batch.index_count==pending_indices_.size())&&std::memcmp(batch.state.render.data(),state_.render.data(),sizeof(state_.render))==0&&std::memcmp(batch.state.stage.data(),state_.stage.data(),sizeof(state_.stage))==0&&std::memcmp(&batch.state.viewport,&state_.viewport,sizeof(state_.viewport))==0&&(transformed||(std::memcmp(batch.state.transform.data(),state_.transform.data(),sizeof(state_.transform))==0&&std::memcmp(&batch.state.material,&state_.material,sizeof(state_.material))==0));
   if(compatible){const size_t oldBytes=pending_vertex_bytes_.size(),addedBytes=(size_t)stride*vertexCount;pending_vertex_bytes_.resize(oldBytes+addedBytes);copy_vertices(pending_vertex_bytes_.data()+oldBytes,addedBytes);if(indexCount){const WORD base=(WORD)batch.vertex_count;pending_indices_.reserve(pending_indices_.size()+indexCount);for(DWORD n=0;n<indexCount;++n)pending_indices_.push_back((WORD)(base+indices[n]));batch.index_count+=indexCount;}batch.vertex_count+=vertexCount;++batch.legacy_draw_count;if(profileCapture){LARGE_INTEGER captureEnd{};QueryPerformanceCounter(&captureEnd);dd_->renderer.record_capture((std::uint64_t)(captureEnd.QuadPart-captureStart.QuadPart),captureBytes,true);}return D3D_OK;}
  }
  LegacyDrawPacket packet{};packet.type=type;packet.fvf=fvf;packet.vertex_count=vertexCount;packet.vertex_offset=pending_vertex_bytes_.size();const size_t addedBytes=(size_t)stride*vertexCount;pending_vertex_bytes_.resize(packet.vertex_offset+addedBytes);copy_vertices(pending_vertex_bytes_.data()+packet.vertex_offset,addedBytes);packet.index_count=indexCount;packet.index_offset=pending_indices_.size();if(indices&&indexCount)pending_indices_.insert(pending_indices_.end(),indices,indices+indexCount);packet.state=state_;packet.texture=image;packet.semantic_kind=semantic.kind;packet.caller_rva=semantic.caller_rva;packet.hud_composition=hud;packet.camp_placement=camp::visual;packet.camp_placement_active=camp::composing&&target_&&target_->compositionOutput();
  if(held)held->AddRef();
  try{if(held)pending_texture_refs_.push_back(held);}catch(...){if(held)held->Release();throw;}
  try{pending_draws_.push_back(std::move(packet));}catch(...){if(held){pending_texture_refs_.pop_back();held->Release();}throw;}
  if(profileCapture){LARGE_INTEGER captureEnd{};QueryPerformanceCounter(&captureEnd);dd_->renderer.record_capture((std::uint64_t)(captureEnd.QuadPart-captureStart.QuadPart),captureBytes,false);}
  return D3D_OK;
 }catch(const std::bad_alloc&){return DDERR_OUTOFMEMORY;}
}
HRESULT Device::GetTexture(DWORD s,LPDIRECTDRAWSURFACE7* out){if(!out||s>=textures_.size())return DDERR_INVALIDPARAMS;*out=textures_[s];if(*out)(*out)->AddRef();return D3D_OK;}
HRESULT Device::SetTexture(DWORD s,LPDIRECTDRAWSURFACE7 t){trace("Device.SetTexture","stage=%lu",s);if(s>=textures_.size())return DDERR_INVALIDPARAMS;auto* p=dynamic_cast<Surface*>(t);if(t&&!p)return DDERR_INVALIDPARAMS;if(diagnostic_trace_enabled()){char event[160]{};if(p){auto* image=p->image();sprintf_s(event,"Diagnostic.TextureBinding.s%lu.%ux%u.f%08lx.c%08lx.b%lu",s,image->width,image->height,image->format.dwFlags,image->format.dwFourCC,image->format.dwRGBBitCount);}else sprintf_s(event,"Diagnostic.TextureBinding.s%lu.null",s);trace(event);}if(recording_){if(recording_texture_mask_[s]&&recording_textures_[s]==p)return D3D_OK;if(p)p->AddRef();if(recording_textures_[s])recording_textures_[s]->Release();recording_textures_[s]=p;recording_texture_mask_[s]=true;return D3D_OK;}if(textures_[s]==p)return D3D_OK;if(p)p->AddRef();auto* previous=textures_[s];textures_[s]=p;if(previous)previous->Release();return D3D_OK;}
HRESULT Device::GetTextureStageState(DWORD s,D3DTEXTURESTAGESTATETYPE t,LPDWORD v){if(!v||s>=state_.stage.size()||(DWORD)t>=state_.stage[s].size())return DDERR_INVALIDPARAMS;*v=state_.stage[s][t];return D3D_OK;}
HRESULT Device::SetTextureStageState(DWORD s,D3DTEXTURESTAGESTATETYPE t,DWORD v){trace("Device.SetTextureStageState","stage=%lu state=%lu value=%08lx",s,(DWORD)t,v);if(diagnostic_trace_enabled()){char event[112]{};sprintf_s(event,"Diagnostic.TextureStageState.%lu.%lu.%08lx",s,(DWORD)t,v);trace(event);}if(s>=state_.stage.size()||(DWORD)t>=state_.stage[s].size())return DDERR_INVALIDPARAMS;if(recording_){recording_state_.stage[s][t]=v;recording_stage_mask_[s][t]=true;}else state_.stage[s][t]=v;return D3D_OK;}
HRESULT Device::BeginStateBlock(){trace("Device.BeginStateBlock");if(recording_)return D3DERR_INBEGINSTATEBLOCK;releaseRecordingTextures();recording_state_=FixedState{};recording_render_mask_.fill(false);for(auto& stage:recording_stage_mask_)stage.fill(false);recording_texture_mask_.fill(false);recording_transform_mask_.fill(false);recording_viewport_mask_=recording_material_mask_=false;recording_=true;return D3D_OK;}
HRESULT Device::EndStateBlock(LPDWORD out){trace("Device.EndStateBlock");if(!recording_)return D3DERR_NOTINBEGINSTATEBLOCK;if(!out)return DDERR_INVALIDPARAMS;DWORD handle=next_block_++;if(!handle)handle=next_block_++;auto& block=blocks_[handle];releaseBlock(block);block=StateBlock{};block.type=D3DSBT_ALL;block.state=recording_state_;block.render_mask=recording_render_mask_;block.stage_mask=recording_stage_mask_;block.texture_mask=recording_texture_mask_;block.transform_mask=recording_transform_mask_;block.viewport_mask=recording_viewport_mask_;block.material_mask=recording_material_mask_;for(size_t i=0;i<recording_textures_.size();++i)if(block.texture_mask[i]){block.textures[i]=recording_textures_[i];if(block.textures[i])block.textures[i]->AddRef();}releaseRecordingTextures();recording_=false;*out=handle;if(state_trace_enabled()){char event[192]{};sprintf_s(event,"Diagnostic.StateBlockEnd.h%lu.am%d.af%lu.ae%d.ar%d.tm%d",handle,block.render_mask[D3DRENDERSTATE_ALPHAFUNC]?1:0,block.state.render[D3DRENDERSTATE_ALPHAFUNC],block.render_mask[D3DRENDERSTATE_ALPHATESTENABLE]?1:0,block.render_mask[D3DRENDERSTATE_ALPHAREF]?1:0,block.texture_mask[0]?1:0);trace(event);}return D3D_OK;}
HRESULT Device::CreateStateBlock(D3DSTATEBLOCKTYPE type,LPDWORD out){trace("Device.CreateStateBlock","type=%lu",(DWORD)type);if(recording_)return D3DERR_INBEGINSTATEBLOCK;if(!out||(type!=D3DSBT_ALL&&type!=D3DSBT_PIXELSTATE&&type!=D3DSBT_VERTEXSTATE))return DDERR_INVALIDPARAMS;DWORD handle=next_block_++;if(!handle)handle=next_block_++;auto& block=blocks_[handle];releaseBlock(block);block=StateBlock{};block.type=type;const bool pixel=type==D3DSBT_ALL||type==D3DSBT_PIXELSTATE,vertex=type==D3DSBT_ALL||type==D3DSBT_VERTEXSTATE;if(pixel){block.render_mask.fill(true);for(auto& stage:block.stage_mask)stage.fill(true);block.texture_mask.fill(true);}if(vertex){block.transform_mask.fill(true);block.viewport_mask=block.material_mask=true;}captureBlock(block);*out=handle;return D3D_OK;}
HRESULT Device::CaptureStateBlock(DWORD handle){trace("Device.CaptureStateBlock","handle=%lu",handle);if(recording_)return D3DERR_INBEGINSTATEBLOCK;auto it=blocks_.find(handle);if(it==blocks_.end())return D3DERR_INVALIDSTATEBLOCK;captureBlock(it->second);return D3D_OK;}
HRESULT Device::ApplyStateBlock(DWORD handle){trace("Device.ApplyStateBlock","handle=%lu",handle);if(recording_)return D3DERR_INBEGINSTATEBLOCK;auto it=blocks_.find(handle);if(it==blocks_.end())return D3DERR_INVALIDSTATEBLOCK;applyBlock(handle,it->second);return D3D_OK;}
HRESULT Device::DeleteStateBlock(DWORD handle){trace("Device.DeleteStateBlock","handle=%lu",handle);if(recording_)return D3DERR_INBEGINSTATEBLOCK;auto it=blocks_.find(handle);if(it==blocks_.end())return D3DERR_INVALIDSTATEBLOCK;releaseBlock(it->second);blocks_.erase(it);return D3D_OK;}

} // namespace owned

namespace owned {
// Issue #70: another module can rebind game.exe's ddraw.dll imports so this
// DLL is loaded but never called. The imports are logged at load and at the
// first enumeration; the first create verifies every installed hook; a
// process that never created through us says so once at exit.
std::atomic<bool> g_ddraw_create_called{false};
std::atomic<bool> g_ddraw_enumerate_called{false};
void note_ddraw_create() noexcept {
 if(!g_ddraw_create_called.exchange(true))bridge_verify_renderer_ownership("startup");
}
} // namespace owned

using owned::DD;

extern "C" HRESULT WINAPI DirectDrawCreateEx(GUID*,void** out,REFIID iid,IUnknown* outer){owned::enable_application_dpi_awareness();owned::install_runtime_bridges();owned::note_ddraw_create();owned::apply_initial_game_back_clip();owned::trace("Export.DirectDrawCreateEx");if(!out||outer)return DDERR_INVALIDPARAMS;*out=nullptr;auto* dd=new(std::nothrow)DD();if(!dd)return DDERR_OUTOFMEMORY;HRESULT hr=dd->QueryInterface(iid,out);dd->Release();return hr;}
extern "C" HRESULT WINAPI DirectDrawCreate(GUID*,LPDIRECTDRAW* out,IUnknown* outer){owned::enable_application_dpi_awareness();owned::install_runtime_bridges();owned::note_ddraw_create();owned::apply_initial_game_back_clip();owned::trace("Export.DirectDrawCreate");if(!out||outer)return DDERR_INVALIDPARAMS;*out=nullptr;auto* core=new(std::nothrow)DD();if(!core)return DDERR_OUTOFMEMORY;auto* legacy=new(std::nothrow)owned::LegacyDD(core);if(!legacy){core->Release();return DDERR_OUTOFMEMORY;}*out=static_cast<IDirectDraw*>(legacy);return DD_OK;}
extern "C" HRESULT WINAPI DirectDrawCreateClipper(DWORD,LPDIRECTDRAWCLIPPER*,IUnknown*){owned::trace("Export.DirectDrawCreateClipper");return DDERR_UNSUPPORTED;}
extern "C" HRESULT WINAPI DirectDrawEnumerateExA(LPDDENUMCALLBACKEXA cb,LPVOID c,DWORD){owned::trace("Export.DirectDrawEnumerateExA");if(!owned::g_ddraw_enumerate_called.exchange(true))owned::bridge_log_ddraw_imports("first_enumerate");if(!cb)return DDERR_INVALIDPARAMS;char desc[]="Owned D3D11 renderer";char name[]="owned-d3d11";cb(nullptr,desc,name,c,nullptr);return DD_OK;}
extern "C" HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA cb,LPVOID c){owned::trace("Export.DirectDrawEnumerateA");if(!cb)return DDERR_INVALIDPARAMS;char desc[]="Owned D3D11 renderer";char name[]="owned-d3d11";cb(nullptr,desc,name,c);return DD_OK;}
extern "C" HRESULT WINAPI DirectDrawEnumerateW(LPDDENUMCALLBACKW,LPVOID){return DDERR_UNSUPPORTED;}
extern "C" HRESULT WINAPI DirectDrawEnumerateExW(LPDDENUMCALLBACKEXW,LPVOID,DWORD){return DDERR_UNSUPPORTED;}
extern "C" void WINAPI AcquireDDThreadLock(){}
extern "C" void WINAPI ReleaseDDThreadLock(){}
extern "C" HRESULT WINAPI CompleteCreateSysmemSurface(){return E_NOTIMPL;}
extern "C" HRESULT WINAPI D3DParseUnknownCommand(){return E_NOTIMPL;}
extern "C" HRESULT WINAPI DDInternalLock(){return E_NOTIMPL;}
extern "C" HRESULT WINAPI DDInternalUnlock(){return E_NOTIMPL;}
extern "C" HRESULT WINAPI DllCanUnloadNow(){return S_FALSE;}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID,REFIID,LPVOID*){return CLASS_E_CLASSNOTAVAILABLE;}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID reserved){if(reason==DLL_PROCESS_ATTACH){owned::g_module=h;DisableThreadLibraryCalls(h);owned::g_exception_handler=AddVectoredExceptionHandler(1,owned::exception_trace);owned::dinput_guard::g_handler=AddVectoredExceptionHandler(1,owned::dinput_guard::on_exception);std::set_terminate(owned::terminate_trace);_set_purecall_handler(owned::purecall_trace);owned::bridge_log_ddraw_imports("dll_attach");}else if(reason==DLL_PROCESS_DETACH){if(!owned::g_ddraw_create_called.load())owned::bridge_log_never_called_at_exit();if(!reserved){owned::stall_watchdog::stop();owned::fear_watch::uninstall();owned::ui_layers_uninstall();owned::uninstall_res_mapping_hooks();if(owned::uninstall_game_focus()&&owned::EngineBridge::instance().uninstall())owned::widescreen_uninstall();}if(owned::dinput_guard::g_handler){RemoveVectoredExceptionHandler(owned::dinput_guard::g_handler);owned::dinput_guard::g_handler=nullptr;}if(owned::g_exception_handler){RemoveVectoredExceptionHandler(owned::g_exception_handler);owned::g_exception_handler=nullptr;}}return TRUE;}
namespace owned { void window_sampler_present(){ window_sampler::present(); } } // TEMPORARY
