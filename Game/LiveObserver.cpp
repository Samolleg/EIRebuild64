#include "StdAfx.h"
#include "ModFinalGolem.h"
#include "LiveObserver.h"
#include "AgentBackground.h"
#include "../external/owned-renderer/Source/native_lod_observer.h"
#include "../external/owned-renderer/Source/engine_bridge.h"
#include "RuntimePaths64.h"
#include "GameJournal.h"
#include "CorePerf.h"
#include "WorldServer.h"
#include "WorldClient.h"
#include "WorldInterface.h"
#include "PlayerClient.h"
#include "PlayerServer.h"
#include "GlobalMap.h"
#include "SaveSlot.h"
#include "Camera.h"
#include "I3DMain.h"
#include "I3DScreen.h"
#include "GameMain.h"
#include "OwnedRender/E2Host.h"
#include "OwnedRender/E3Input.h"
#include "Unit.h"
#include "UnitServer.h"
#include "UnitDatabase.h"
#include "SpellEffect.h"
#include "../Navigation/LiveQuery.h"
#include "nlohmann/json.hpp"
#include <sddl.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_set>

#pragma comment(lib,"windowscodecs.lib")
using nlohmann::json;
using Microsoft::WRL::ComPtr;

namespace LiveObserver {
namespace {
using Clock=std::chrono::steady_clock;
constexpr size_t RequestLimit=16384, ResponseLimit=262144, ImageLimit=4096u*4096u*4u;
constexpr auto FrameBudget=std::chrono::microseconds(800);
constexpr size_t UnitLimit=200, VariableLimit=200, PathLimit=64, EffectLimit=32;
struct Job {
    json request, response;
    std::string method;
    std::mutex mutex;
    std::condition_variable ready;
    bool done=false;
    bool readyToFinish=false; // main thread only
    bool quitRequested=false; // quit through the ordinary path after its reply
    unsigned long long operationId=0;
    std::atomic<bool> replySent{false};
    std::atomic<bool> cancelled{false};
    unsigned phase=0;
    size_t bucket=0, offset=0, item=0;
    size_t bucketCount=0, initialCount=0;
    DWORD unitNextKey=0;
    bool unitScanStarted=false,unitScanFinished=false;
    ObjectId worldId=0;
    const CWorldServer* world=nullptr;
    std::unordered_set<int> seen;
    std::unique_ptr<dtNavMeshQuery,Navigation::QueryDeleter> query;
    std::unique_ptr<Navigation::LiveQuery> navigation;
    const Navigation::Mesh* mesh=nullptr;
    std::uint64_t navRevision=0;
    int navProfile=0;
    std::shared_ptr<const owned::NativeLodSnapshot> lodSnapshot;
    std::uint64_t lodRequest=0;
    Clock::time_point lodExpires{};
    std::vector<unsigned char> image;
    // The pipe worker allocates a staging texture using only this copied
    // descriptor/device; it never inspects a game object or context.
    ComPtr<ID3D11Device> captureDevice;
    D3D11_TEXTURE2D_DESC captureDesc{};
    std::atomic<int> setup{0}; // 1 requested, 4 allocating, 2 ready, 3 failed
    std::atomic<ID3D11Texture2D*> prepared{nullptr};
    HRESULT setupError=S_OK;
    ComPtr<ID3D11Texture2D> staging;
    ComPtr<ID3D11DeviceContext> context;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    UINT width=0,height=0,row=0;
    bool isMapped=false;
    bool budgetExceeded=false;
    Clock::duration captureCost{};
    ~Job(){if(auto* texture=prepared.exchange(nullptr))texture->Release();}
};
std::mutex queueMutex;
std::shared_ptr<Job> queued;
std::shared_ptr<Job> active; // main thread only
HANDLE stopEvent=nullptr;
std::thread worker;
// One operation record, copied only on a request or transition/error event.
// It owns no game pointers and adds no per-frame polling or telemetry buffers.
struct OperationState {
    unsigned long long id=0;
    std::string method, parameter, state="idle", stage="idle", error, expectedZone;
    bool inFlight=false, executing=false, applied=false;
};
std::mutex operationMutex;
OperationState operation;
unsigned long long operationSequence=0;
bool saveIsolated=false; // game thread only; deliberately survives load/new game
bool IsCommand(const std::string& method) {
    return method=="load_save"||method=="new_game"||method=="enter_zone"||method=="save_game"||
           method=="unit_move"||method=="unit_teleport"||method=="pause"||method=="quit"||
           method=="set_global"||method=="object_transform"||method=="god_mode"||method=="script_run"||method=="final_golem_action";
}

json Error(const char* message) {return {{"ok",false},{"error",message},{"truncated",false}};}
void ChannelError(const char* operation,DWORD code) noexcept {
    GameJournal::Event("ERROR","live_channel","operation=%s code=%lu",operation,static_cast<unsigned long>(code));
}
std::string Utf8(const wchar_t* source,int length=-1) {
    if(!source)return {};
    const int count=WideCharToMultiByte(CP_UTF8,0,source,length,nullptr,0,nullptr,nullptr);
    if(count<=0)return {};
    std::string result(size_t(count),'\0');
    WideCharToMultiByte(CP_UTF8,0,source,length,result.data(),count,nullptr,nullptr);
    if(length<0&&!result.empty()&&result.back()=='\0')result.pop_back();
    return result;
}
std::string Text(const char* source,size_t maxBytes=512) {
    if(!source)return {};
    const int bytes=int(strnlen_s(source,maxBytes));
    if(!bytes)return {};
    const int count=MultiByteToWideChar(1251,0,source,bytes,nullptr,0);
    if(count<=0)return {};
    std::wstring wide(size_t(count),L'\0');
    MultiByteToWideChar(1251,0,source,bytes,wide.data(),count);
    return Utf8(wide.data(),count);
}
json OperationJsonLocked() {
    return {{"id",operation.id},{"method",operation.method},{"name",operation.parameter},
            {"state",operation.state},{"stage",operation.stage},{"error",operation.error},
            {"in_flight",operation.inFlight}};
}
json OperationJson() {
    std::lock_guard<std::mutex> lock(operationMutex);return OperationJsonLocked();
}
json ExecutingStatus() {
    std::lock_guard<std::mutex> lock(operationMutex);
    if(!operation.inFlight||(!operation.executing&&operation.applied))return nullptr;
    json result={{"ok",operation.error.empty()},{"pid",GetCurrentProcessId()},
                 {"status_scope","operation_only"},{"main_thread_responding",false},{"god",nullptr},
                 {"operation",OperationJsonLocked()},{"truncated",false}};
    if(!operation.error.empty())result["error"]="game is not responding; see journal";
    return result;
}
unsigned long long BeginOperation(const std::string& method,const std::string& parameter) {
    std::lock_guard<std::mutex> lock(operationMutex);
    if(operation.inFlight)throw std::runtime_error("another game operation is in progress");
    operation=OperationState{};operation.id=++operationSequence;
    operation.method=method;operation.parameter=parameter;operation.state="running";
    operation.stage="queued";operation.inFlight=true;return operation.id;
}
void LogCommand(const std::string& method,const json& params,const json& response) noexcept {
    if(!IsCommand(method))return;
    try {
        // ASCII JSON keeps user strings intact through the CP1251 journal API.
        const auto arguments=params.dump(-1,' ',true,json::error_handler_t::replace);
        const auto result=response.dump(-1,' ',true,json::error_handler_t::replace);
        GameJournal::Event("MCP","mcp_cmd","source=mcp command=%s params=%s result=%s",
            method.c_str(),GameJournal::Quote(arguments.c_str()).c_str(),GameJournal::Quote(result.c_str()).c_str());
    }catch(...){ChannelError("command_log",0);}
}
void ReleaseLodRequest(Job& job) noexcept {
    if(!job.lodRequest)return;
    owned::native_lod_acknowledge(job.lodRequest);job.lodRequest=0;
}
bool Finish(Job& job) {
    std::unique_lock<std::mutex> lock(job.mutex,std::try_to_lock);
    if(!lock.owns_lock())return false;
    ReleaseLodRequest(job);
    job.done=true;lock.unlock();job.ready.notify_one();return true;
}
void CleanupCapture(Job& job) noexcept {
    ReleaseLodRequest(job);
    if(job.isMapped&&job.context&&job.staging)job.context->Unmap(job.staging.Get(),0);
    job.isMapped=false;job.staging.Reset();job.context.Reset();
    if(auto* texture=job.prepared.exchange(nullptr))texture->Release();
}
bool Step(Job& job,Clock::time_point deadline);

// All I/O waits are overlapped so shutdown and slow/missing clients cannot
// strand the game thread. One bounded request is accepted per connection.
bool WaitIo(HANDLE pipe,OVERLAPPED& io,DWORD timeout,DWORD& transferred) {
    HANDLE handles[]={io.hEvent,stopEvent};
    const DWORD wait=WaitForMultipleObjects(2,handles,FALSE,timeout);
    if(wait!=WAIT_OBJECT_0) {
        CancelIoEx(pipe,&io);
        GetOverlappedResult(pipe,&io,&transferred,TRUE);
        return false;
    }
    return !!GetOverlappedResult(pipe,&io,&transferred,FALSE);
}
bool Transfer(HANDLE pipe,bool write,void* data,DWORD length,DWORD& transferred) {
    OVERLAPPED io{};io.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!io.hEvent)return false;
    const BOOL immediate=write?WriteFile(pipe,data,length,&transferred,&io):ReadFile(pipe,data,length,&transferred,&io);
    bool ok=!!immediate;
    if(!ok&&GetLastError()==ERROR_IO_PENDING)ok=WaitIo(pipe,io,5000,transferred);
    CloseHandle(io.hEvent);return ok;
}
bool Connect(HANDLE pipe) {
    OVERLAPPED io{};io.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!io.hEvent)return false;
    BOOL immediate=ConnectNamedPipe(pipe,&io);
    bool ok=!!immediate;
    if(!ok) {
        const auto error=GetLastError();DWORD count=0;
        if(error==ERROR_PIPE_CONNECTED)ok=true;
        else if(error==ERROR_IO_PENDING)ok=WaitIo(pipe,io,INFINITE,count);
    }
    CloseHandle(io.hEvent);return ok;
}
PSECURITY_DESCRIPTOR UserOnlySecurity() {
    HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return nullptr;
    DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
    std::vector<unsigned char> storage(bytes);
    const bool ok=bytes&&GetTokenInformation(token,TokenUser,storage.data(),bytes,&bytes);
    CloseHandle(token);if(!ok)return nullptr;
    LPWSTR sid=nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid,&sid))return nullptr;
    const std::wstring sddl=std::wstring(L"D:P(A;;GA;;;")+sid+L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR result=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&result,nullptr))return nullptr;
    return result;
}

std::string SavePng(Job& job) {
    namespace fs=std::filesystem;
    if(job.width==0||job.height==0||size_t(job.width)*job.height*4>job.image.size())throw std::runtime_error("invalid image dimensions");
    for(size_t i=3,end=size_t(job.width)*job.height*4;i<end;i+=4)job.image[i]=255;
    const auto init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(init))throw std::runtime_error("WIC COM initialization failed");
    struct ComEnd {~ComEnd(){CoUninitialize();}} comEnd;
    const auto dir=RuntimePaths64::Directory(L"Logs64")/L"live";fs::create_directories(dir);
    const auto stem=L"eimcp-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(GetCurrentProcessId());
    const auto temp=dir/(stem+L".tmp");const auto file=dir/(stem+L".png");
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);
    if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(temp.c_str(),GENERIC_WRITE);
    if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
    if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,nullptr);
    if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);
    if(SUCCEEDED(hr))hr=frame->SetSize(job.width,job.height);
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
    if(SUCCEEDED(hr)&&format!=GUID_WICPixelFormat32bppBGRA)hr=E_FAIL;
    if(SUCCEEDED(hr))hr=frame->WritePixels(job.height,job.width*4,job.width*job.height*4,job.image.data());
    if(SUCCEEDED(hr))hr=frame->Commit();
    if(SUCCEEDED(hr))hr=encoder->Commit();
    frame.Reset();encoder.Reset();stream.Reset();factory.Reset();
    if(FAILED(hr)){DeleteFileW(temp.c_str());throw std::runtime_error("PNG encoding failed");}
    if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());throw std::runtime_error("PNG finalization failed");}
    std::vector<fs::directory_entry> captures;
    for(const auto& entry:fs::directory_iterator(dir)) {
        const auto name=entry.path().filename().wstring();
        if(entry.is_regular_file()&&entry.path().extension()==L".png"&&name.rfind(L"eimcp-",0)==0) {
            const auto stem=entry.path().stem().wstring();const auto dash=stem.find(L'-',6);
            const auto digits=[](wchar_t c){return c>=L'0'&&c<=L'9';};
            if(dash!=std::wstring::npos&&dash>6&&dash+1<stem.size()&&
               std::all_of(stem.begin()+6,stem.begin()+dash,digits)&&std::all_of(stem.begin()+dash+1,stem.end(),digits))captures.push_back(entry);
        }
    }
    std::sort(captures.begin(),captures.end(),[](const auto& a,const auto& b){return a.last_write_time()>b.last_write_time();});
    for(size_t i=10;i<captures.size();++i)fs::remove(captures[i]);
    return Utf8(file.c_str());
}

void Serve(HANDLE pipe) {
    std::string line;line.reserve(1024);char buffer[1024];
    while(line.size()<=RequestLimit&&line.find('\n')==std::string::npos) {
        DWORD count=0;if(!Transfer(pipe,false,buffer,sizeof(buffer),count)||!count){ChannelError("read",GetLastError());return;}
        line.append(buffer,count);
    }
    json response;std::string command;json arguments;std::shared_ptr<Job> replyJob;
    try {
        const auto end=line.find('\n');
        if(end==std::string::npos||end+1!=line.size()||end>=RequestLimit)throw std::runtime_error("invalid or oversized request");
        auto job=std::make_shared<Job>();replyJob=job;job->request=json::parse(line.begin(),line.begin()+end);
        if(!job->request.is_object()||!job->request.contains("method")||!job->request["method"].is_string())throw std::runtime_error("invalid request");
        const std::string method=job->request["method"].get<std::string>();job->method=method;command=method;
        if(!job->request.contains("params"))job->request["params"]=json::object();
        if(!job->request["params"].is_object())throw std::runtime_error("params must be an object");
        arguments=job->request["params"];
        if(method=="status")response=ExecutingStatus();
        if(response.is_null()) {
        if(method=="nav"||method=="nav_path") {
            // init(nullptr) allocates independent Detour scratch on this thread;
            // it does not inspect, attach to, or dereference a game mesh.
            job->query.reset(dtAllocNavMeshQuery());
            if(!job->query||dtStatusFailed(job->query->init(nullptr,65535)))throw std::runtime_error("query allocation failed");
        } else if(method=="screenshot")job->image.resize(ImageLimit);
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if(queued)throw std::runtime_error("observer busy");
            queued=job;
        }
        std::unique_lock<std::mutex> lock(job->mutex);
        const auto expires=Clock::now()+std::chrono::milliseconds(4500);
        for(;;) {
            if(!job->ready.wait_until(lock,expires,[&]{return job->done||job->cancelled||job->setup.load(std::memory_order_acquire)==1||WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0;})) {
                job->cancelled=true;response=Error("game is not responding; see journal");
                response["main_thread_responding"]=false;response["operation"]=OperationJson();break;
            }
            if(job->done){response=std::move(job->response);break;}
            if(job->cancelled){response=Error("observer request cancelled");break;}
            if(WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0){job->cancelled=true;response=Error("game is shutting down");break;}
            if(job->setup.load(std::memory_order_acquire)==1) {
                job->setup.store(4,std::memory_order_release);
                auto device=job->captureDevice;const auto desc=job->captureDesc;
                lock.unlock();
                ComPtr<ID3D11Texture2D> texture;
                const HRESULT hr=device?device->CreateTexture2D(&desc,nullptr,&texture):E_FAIL;
                lock.lock();
                job->setupError=hr;
                if(SUCCEEDED(hr))job->prepared.store(texture.Detach(),std::memory_order_release);
                job->setup.store(SUCCEEDED(hr)?2:3,std::memory_order_release);
            }
        }
        lock.unlock();
        if(method=="screenshot"&&response.value("ok",false))response["path"]=SavePng(*job);
        }
    }catch(const std::exception& e) {
        GameJournal::Event("ERROR","live_request","reason=%s",GameJournal::Quote(e.what()).c_str());
        response=Error(e.what());
    }catch(...) {ChannelError("request_exception",0);response=Error("live request failed");}
    LogCommand(command,arguments,response);
    auto payload=response.dump(-1,' ',false,json::error_handler_t::replace);
    if(payload.size()+1>ResponseLimit)payload=Error("response exceeds size limit").dump();
    payload.push_back('\n');DWORD count=0;
    const bool sent=Transfer(pipe,true,payload.data(),DWORD(payload.size()),count)&&count==payload.size();
    if(!sent)ChannelError("write",GetLastError());
    if(replyJob&&replyJob->cancelled.load()) {
        std::lock_guard<std::mutex> lock(queueMutex);
        if(queued==replyJob)queued.reset();
    }
    if(sent) {
        // WriteFile completion only means the bytes entered the pipe buffer.
        // The existing client closes after consuming its complete response.
        // Wait for that close using bounded, cancellable worker I/O before
        // DisconnectNamedPipe can discard the unread tail of a large reply.
        char unused=0;DWORD ignored=0;Transfer(pipe,false,&unused,1,ignored);
    }
    if(replyJob)replyJob->replySent.store(true,std::memory_order_release);
}
void Run() noexcept {
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    try {
        descriptor=UserOnlySecurity();
        if(!descriptor){ChannelError("security_descriptor",GetLastError());return;}
        SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};
        const auto name=std::wstring(L"\\\\.\\pipe\\EIRebuild64.live.")+std::to_wstring(GetCurrentProcessId());
        HANDLE pipe=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,DWORD(ResponseLimit),DWORD(RequestLimit),0,&security);
        if(pipe==INVALID_HANDLE_VALUE){ChannelError("create_pipe",GetLastError());LocalFree(descriptor);return;}
        while(WaitForSingleObject(stopEvent,0)!=WAIT_OBJECT_0) {
            if(Connect(pipe))Serve(pipe);
            DisconnectNamedPipe(pipe);
        }
        CloseHandle(pipe);
    } catch(...) {ChannelError("worker_exception",0);}
    if(descriptor)LocalFree(descriptor);
}
} // namespace

bool IsOperation(const char* method) noexcept {
    std::lock_guard<std::mutex> lock(operationMutex);
    return operation.inFlight&&operation.method==method;
}
void OperationExecuting(const char* method) noexcept {
    std::lock_guard<std::mutex> lock(operationMutex);
    if(operation.inFlight&&operation.method==method){operation.executing=true;operation.stage="executing";}
}
void OperationApplied(const char* method,bool success) noexcept {
    try {
        std::lock_guard<std::mutex> lock(operationMutex);
        if(!operation.inFlight||operation.method!=method)return;
        operation.executing=false;
        if(!success||!operation.error.empty()) {
            if(operation.error.empty())operation.error="game operation failed; see journal";
            operation.state="error";operation.stage="failed";operation.inFlight=false;return;
        }
        operation.applied=true;
        operation.expectedZone=theWorldServer.GetCurrentZoneName();
        if(operation.method=="enter_zone"&&_stricmp(operation.expectedZone.c_str(),operation.parameter.c_str())!=0) {
            operation.error="game entered a different zone";operation.state="error";
            operation.stage="failed";operation.inFlight=false;return;
        }
        if(operation.method=="save_game") {
            operation.state="done";operation.stage="done";operation.inFlight=false;
        } else operation.stage="connecting_client";
    }catch(...){ChannelError("operation_result",0);}
}
void RecordError(const char* text,const char* extra) noexcept {
    try {
        std::lock_guard<std::mutex> lock(operationMutex);
        if(!operation.inFlight) {
            if(!AgentBackground::Enabled()||operation.state=="error")return;
            operation=OperationState{};operation.id=++operationSequence;operation.method="background_error";
        }
        if(!operation.error.empty())return;
        operation.error=Text(text,2048)+Text(extra,512);operation.state="error";
        operation.stage="game_error";
        // Notification's journal write and critical flush happen before the modal UI.
    }catch(...){ChannelError("operation_error",0);}
}
std::string ProtectedSaveSlot(const char* name) {
    if(!name)return {};
    if((!saveIsolated&&!AgentBackground::Enabled())||strncmp(name,"mcp_",4)==0)return name;
    return std::string("mcp_")+name;
}
void Start() noexcept {
    try {
        if(stopEvent||worker.joinable())return;
        stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!stopEvent){ChannelError("stop_event",GetLastError());return;}
        worker=std::thread(&Run);
    } catch(...) {
        ChannelError("start",GetLastError());
        if(stopEvent){CloseHandle(stopEvent);stopEvent=nullptr;}
    }
}
void Stop() noexcept {
    if(!stopEvent)return;
    SetEvent(stopEvent);
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if(queued){queued->cancelled=true;queued->ready.notify_one();}
    }
    if(active){active->cancelled=true;active->ready.notify_one();CleanupCapture(*active);}
    if(worker.joinable())worker.join();
    {std::lock_guard<std::mutex> lock(queueMutex);queued.reset();}
    active.reset();CloseHandle(stopEvent);stopEvent=nullptr;
}
void Frame() noexcept {
    try {
        if(!active) {
            std::unique_lock<std::mutex> lock(queueMutex,std::try_to_lock);
            if(lock.owns_lock()&&queued)active=std::move(queued);
        }
        if(!active)return;
        if(active->cancelled){CleanupCapture(*active);active.reset();return;}
        if(active->readyToFinish){
            if(active->quitRequested&&active->replySent.load(std::memory_order_acquire)){
                if(!NGame::QuitProgram(0))ChannelError("quit_refused",0);
                active.reset();return;
            }
            if(Finish(*active)&&!active->quitRequested)active.reset();return;
        }
        const auto started=Clock::now();
        const auto used=active->captureCost;active->captureCost=Clock::duration{};
        if(used>std::chrono::milliseconds(1)&&!IsCommand(active->method)) {
            if(active->method!="lod")GameJournal::Event("ERROR","live_budget","elapsed_us=%lld",static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(used).count()));
            active->budgetExceeded=true;
        }
        const auto remaining=used<FrameBudget?FrameBudget-used:Clock::duration{};
        const auto deadline=started+remaining;
        if(remaining==Clock::duration{})return;
        bool done=false;
        try {done=Step(*active,deadline);}
        catch(const std::exception& e) {
            if(active->method!="lod")GameJournal::Event("ERROR","live_observe","reason=%s",GameJournal::Quote(e.what()).c_str());
            if(active->operationId){RecordError(e.what(),"");OperationApplied(active->method.c_str(),false);}
            CleanupCapture(*active);active->response=Error(e.what());done=true;
        } catch(...) {
            if(active->method!="lod")ChannelError("observe_exception",0);
            if(active->operationId){RecordError("game command failed","");OperationApplied(active->method.c_str(),false);}
            CleanupCapture(*active);active->response=Error("live request failed");done=true;
        }
        const auto elapsed=Clock::now()-started+used;
        if(elapsed>std::chrono::milliseconds(1)&&!IsCommand(active->method)) {
            if(active->method!="lod")GameJournal::Event("ERROR","live_budget","elapsed_us=%lld",static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()));
            active->budgetExceeded=true;
        }
        if(active->cancelled){CleanupCapture(*active);active.reset();return;}
        if(done){
            if(active->budgetExceeded&&active->response.is_object())active->response["budget_exceeded"]=true;
            active->readyToFinish=true;if(Finish(*active)&&!active->quitRequested)active.reset();
        }
    } catch(...) {ChannelError("frame_exception",0);if(active){CleanupCapture(*active);active->cancelled=true;active->ready.notify_one();active.reset();}}
}
bool WantsCapture() noexcept {return active&&!active->cancelled&&(active->phase==1||active->phase==6)&&active->method=="screenshot";}
void CaptureFrame(ID3D11DeviceContext* context,IDXGISwapChain* swap) noexcept {
    if(!WantsCapture())return;
    const auto started=Clock::now();
    try {
        auto& job=*active;
        if(!context||!swap)throw std::runtime_error("backbuffer is unavailable");
        ComPtr<ID3D11Texture2D> output;
        if(FAILED(swap->GetBuffer(0,IID_PPV_ARGS(&output))))throw std::runtime_error("backbuffer is unavailable");
        D3D11_TEXTURE2D_DESC desc{};output->GetDesc(&desc);
        if(!desc.Width||!desc.Height||desc.Width>4096||desc.Height>4096||
           desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM||desc.SampleDesc.Count!=1||
           size_t(desc.Width)*desc.Height*4>job.image.size())throw std::runtime_error("unsupported backbuffer format or dimensions");
        ComPtr<ID3D11Device> device;context->GetDevice(&device);
        if(!device)throw std::runtime_error("backbuffer device is unavailable");
        if(job.phase==1) {
            job.context=context;job.captureDevice=device;job.width=desc.Width;job.height=desc.Height;
            desc.MipLevels=1;desc.ArraySize=1;desc.Usage=D3D11_USAGE_STAGING;
            desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
            job.captureDesc=desc;job.phase=5;
            job.setup.store(1,std::memory_order_release);job.ready.notify_one();
        } else {
            if(desc.Width!=job.width||desc.Height!=job.height||device.Get()!=job.captureDevice.Get()||!job.staging)
                throw std::runtime_error("backbuffer changed during capture; retry");
            job.context=context;context->CopyResource(job.staging.Get(),output.Get());job.phase=2;
        }
    } catch(const std::exception& e) {
        GameJournal::Event("ERROR","live_screenshot","reason=%s",GameJournal::Quote(e.what()).c_str());
        CleanupCapture(*active);
        try {active->response=Error(e.what());active->phase=4;} catch(...) {active->cancelled=true;active->ready.notify_one();}
    } catch(...) {
        ChannelError("screenshot_exception",0);CleanupCapture(*active);
        try {active->response=Error("screenshot failed");active->phase=4;} catch(...) {active->cancelled=true;active->ready.notify_one();}
    }
    active->captureCost+=Clock::now()-started;
}
} // namespace LiveObserver

namespace owned {
bool bridge_live_wants_capture() noexcept {return LiveObserver::WantsCapture();}
void bridge_live_capture(ID3D11DeviceContext* context,IDXGISwapChain* swap) noexcept {
    LiveObserver::CaptureFrame(context,swap);
}
}

// This accessor only reads existing transient state. It creates no game
// objects and has no setters or serialization hooks.
struct LiveSnapshotAccess {
    static unsigned Wait(const CUnitServer& unit) {return unit.navigationWait;}
    static unsigned Rejects(const CUnitServer& unit) {return unit.navigationRejects;}
    static int RunFailures(const CUnitServer& unit) {return unit.nL1RunFailCounter;}
    static int State(const CUnitServer& unit) {return int(unit.eL1State);}
    static const SAIPath& Path(const CUnitServer& unit) {return unit.path;}
};

namespace LiveObserver {
namespace {
json Point(const CPlot3D& p) {return json::array({p.x,p.y,p.z});}
json Point(Navigation::Point p) {return json::array({p.x,p.y,p.z});}
json Location(const Navigation::Location& p) {
    return {{"position",Point(p.position)},{"surface",p.surface},{"polygon",p.polygon},
            {"generation",p.generation},{"height",p.height},{"area",p.area}};
}
CPlayer* Player() {
    auto* client=theWorld.GetPlayer();return client?client->GetPlayer():nullptr;
}
bool Squad(CUnit* unit,CPlayer* player) {
    if(!player)return false;
    for(int i=0;i<player->party.people.GetSize()&&i<64;++i)
        if(player->party.people[i].nID==unit->GetID())return true;
    return false;
}
int PlayerNumber(CPlayer* player) {
    if(!player)return -1;
    const int value=player->GetPlayerNumber();return value>=0&&value<32?value:-1;
}
bool Enemy(CUnit* unit,int playerNumber) {
    return (unit->GetEnemyMask()&unit->GetPossibleEnemiesMask()&(1u<<unsigned(playerNumber)))!=0;
}
const char* Movement(CUnit* unit) {
    const auto type=unit->GetUserUnitStats()->cLocomotion;
    return type==3?"flying":type==2?"levitate":"ground";
}
bool Match(const std::string& text,const std::string& part) {
    if(part.empty())return true;
    auto wide=[](const std::string& value) {
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
        if(length<=0)return std::wstring();
        std::wstring result(size_t(length),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),result.data(),length);return result;
    };
    const auto hay=wide(text),needle=wide(part);
    if(needle.empty()||needle.size()>hay.size())return false;
    for(size_t i=0;i+needle.size()<=hay.size();++i)
        if(CompareStringOrdinal(hay.data()+i,int(needle.size()),needle.data(),int(needle.size()),TRUE)==CSTR_EQUAL)return true;
    return false;
}
json UnitRow(CUnit* unit,CWorldServer& world,CPlayer* player) {
    const auto* prototype=unit->GetPrototype();
    auto* specific=unit->GetUnitSpecific();
    auto* server=specific&&specific->WhoAmI()==HPC_UNITSERVERSPECIFIC?static_cast<CUnitServer*>(specific):nullptr;
    const auto* activeSlot=world.GetActiveSet()->Seek(unit->GetID());
    json row={{"id",unit->GetID()},{"name",Text(unit->GetName(),256)},
        {"prototype",prototype?Text(prototype->szName,256):std::string()},
        {"position",Point(unit->GetCP())},{"hp",unit->GetHP()},{"max_hp",unit->GetMaxHP()},
        {"moving",bool(unit->IsMoving())},{"dead",bool(unit->IsDead())},
        {"locomotion",Movement(unit)},{"pose",int(unit->GetCurrentPose())},
        {"ai_class",unit->GetAIClass()},{"active",activeSlot&&*activeSlot==unit},
        {"squad",Squad(unit,player)},{"enemy",nullptr}};
    if(const int number=PlayerNumber(player);number>=0)row["enemy"]=Enemy(unit,number);
    row["position_z_kind"]="ground_relative";
    if(unit->navLocation.surface)row["navigation_position"]=Point(unit->navLocation.position);
    if(server)row["ai_state"]=int(server->logic.GetScriptState());
    return row;
}
json UnitDetail(CUnit* unit,CWorldServer& world,CPlayer* player) {
    auto result=UnitRow(unit,world,player);
    float w,x,y,z;unit->GetOrientation().GetComponents(w,x,y,z);
    const auto axis=unit->GetYAxis();
    float move=0,turn=0;unit->GetSpeeds(move,turn);
    result["movement_speeds"]={{"move",move},{"turn",turn},{"modifier_enabled",unit->scriptMovementEnabled},
        {"offset",unit->scriptMovementOffset},{"factor",unit->scriptMovementFactor}};
    result["orientation_wxyz"]=json::array({w,x,y,z});
    result["orientation_xy_radians"]=std::atan2(axis.y,axis.x);
    result["nav_location"]=Location(unit->navLocation);
    result["effects"]=json::array();
    const auto& effects=unit->GetSpellEffects();
    const int effectCount=effects.GetSize();
    for(int i=0;i<effectCount&&i<int(EffectLimit);++i) {
        const auto& effect=effects[i];
        result["effects"].push_back({{"type",effect.GetEffectType()},{"duration_segments",effect.nDuration},
                                      {"power",effect.fpPower},{"radius",effect.fpRadius}});
    }
    result["effects_truncated"]=effectCount>int(EffectLimit);
    auto* specific=unit->GetUnitSpecific();
    if(!specific||specific->WhoAmI()!=HPC_UNITSERVERSPECIFIC)return result;
    auto& server=*static_cast<CUnitServer*>(specific);
    const auto& path=LiveSnapshotAccess::Path(server);
    auto* target=server.logic.GetCurrentTarget();
    const auto& command=server.GetScriptCommand();
    result["navigation"]={{"destination",Point(path.ptDestination)},{"finished",path.bFinished},{"turned",path.navigationTurned},
        {"route_failed",path.bRouteFailed},{"movement_state",LiveSnapshotAccess::State(server)},
        {"segment_type",int(path.curSegment.GetMoveType())},{"wait",LiveSnapshotAccess::Wait(server)},
        {"rejects",LiveSnapshotAccess::Rejects(server)},{"run_failures",LiveSnapshotAccess::RunFailures(server)},
        {"path_cost",path.nPathCost},{"points",json::array()},{"truncated",false}};
    auto append=[&](const CAIRoute& route) {
        POSITION pos=route.route.GetHeadPosition();
        while(pos) {
            const auto& node=route.route.GetNext(pos);
            if(result["navigation"]["points"].size()>=PathLimit){result["navigation"]["truncated"]=true;break;}
            result["navigation"]["points"].push_back(json::array({AITile2Meter(node.point.x),AITile2Meter(node.point.y),node.supportZ}));
        }
    };
    append(path.curSegment.route);append(path.toGo);
    const auto& origin=unit->GetCP();
    for(const auto& point:result["navigation"]["points"]) {
        const float dx=point[0].get<float>()-origin.x,dy=point[1].get<float>()-origin.y;
        if(dx*dx+dy*dy>0.0001f){result["navigation"]["next_angle_radians"]=std::atan2(dy,dx);break;}
    }
    result["ai"]={{"state",int(server.logic.GetScriptState())},{"motivation",int(server.logic.GetCurrentMotivationType())},
        {"alarm",server.logic.GetGroupSearchAlarm()},{"enemy_visible",server.logic.bEnemyVisible},
        {"target_id",target?target->GetID():-1},{"command",int(command.cmd)},
        {"command_target_id",command.GetTargetID()},{"command_point",Point(command.ptTarget)}};
    return result;
}
// Readiness is evaluated only for a status or command request, never each frame.
// World construction alone is insufficient: wait for the normal local handshake
// and the client/server copies of the squad to reach the mission.
void UpdateOperationReadiness() {
    std::lock_guard<std::mutex> lock(operationMutex);
    if(!operation.inFlight||!operation.applied||operation.executing)return;
    if(!operation.error.empty()) {
        operation.state="error";operation.stage="game_error";operation.inFlight=false;return;
    }
    auto& world=theWorldServer;auto* client=theWorld.GetPlayer();
    if(!world.bMapLoaded||world.GetServerMode()!=SERV_MODE_LOCAL||!theWorld.IsConnected()||
       !client||IWorldConnector.HasReq()||
       (client->GetStatus()!=CPlayerClient::CPS_ACTIVE&&client->GetStatus()!=CPlayerClient::CPS_CAMP))return;
    auto* player=client->GetPlayer();auto* serverPlayer=world.GetPlayer(0);
    if(!player||!serverPlayer||player->curInterface==IFACE_NONE||
       player->curInterface!=serverPlayer->curInterface||
       world.curInterface!=serverPlayer->curInterface||
       world.GetCurrentZoneName()!=operation.expectedZone.c_str())return;
    auto* server=static_cast<CPlayerServer*>(serverPlayer->GetPlayerSpecific());
    if(!server||(server->GetStatus()!=CPlayerServer::PSS_ACTIVE&&server->GetStatus()!=CPlayerServer::PSS_CAMP))return;
    if(player->curInterface==IFACE_MISSION||player->curInterface==IFACE_BASE) {
        if(player->party.people.GetSize()!=serverPlayer->party.people.GetSize())return;
        for(int i=0;i<player->party.people.GetSize();++i) {
            const int id=player->party.people[i].nID;
            if(id!=serverPlayer->party.people[i].nID)return;
            auto* unit=world.GetUnit(id);auto* clientUnit=theWorld.GetUnit(id);
            if(!unit||unit->IsInvalid()||unit->IsUnreferenced())return;
            // A restored corpse need not remain owned or visible on the client.
            // It is a valid loaded state, including the main hero's death screen.
            if(unit->IsDead())continue;
            if(!clientUnit||clientUnit->IsInvalid()||clientUnit->IsUnreferenced()||
               !Squad(unit,serverPlayer)||unit->GetOwner()!=serverPlayer||clientUnit->GetOwner()!=player)return;
            auto* specific=unit->GetUnitSpecific();
            if(!specific||specific->WhoAmI()!=HPC_UNITSERVERSPECIFIC)return;
        }
    }
    operation.state="done";operation.stage="done";operation.inFlight=false;
}
json Status() {
    UpdateOperationReadiness();
    auto& world=theWorldServer;const auto& perf=CorePerf::Get();
    const auto currentOperation=OperationJson();
    if(!world.bMapLoaded&&currentOperation.at("in_flight").get<bool>())
        return {{"ok",true},{"pid",GetCurrentProcessId()},
                {"status_scope","operation_only"},{"main_thread_responding",true},
                {"map_loaded",false},{"god",nullptr},{"operation",currentOperation},{"truncated",false}};
    static const char* const screens[]={"none","mission","main_menu","base","global_map","camp","autoplay","debug","endgame","exchange"};
    const int screen=int(world.curInterface);
    const bool combat=world.IsCombatPause(),modal=theInterface.GetModalPause(),editor=OwnedRender::E3EditorPaused();
    const bool background=OwnedRender::E2BackgroundPaused(NGame::hWnd);
    const auto* player=world.bMapLoaded?world.GetPlayer(0):nullptr;
    json result={{"ok",true},{"pid",GetCurrentProcessId()},{"map_loaded",bool(world.bMapLoaded)},
                 {"mode",screen},{"screen",screen>=0&&screen<10?screens[screen]:"unknown"},
                 {"server_mode",int(world.GetServerMode())},{"god",player?json(int(player->bGod)):json(nullptr)},
                 {"paused",combat||modal||editor||background},
                 {"pause_reasons",{{"combat",combat},{"modal",modal},{"editor",editor},{"background",background}}},
                 {"camera",Point(theCamera.GetCP())},{"camera_anchor",Point(theCamera.GetAnchorPoint())},
                 {"truncated",false}};
    result["operation"]=currentOperation;result["save_isolation"]=saveIsolated||AgentBackground::Enabled();
    result["background_agent"]=AgentBackground::Enabled();
    result["main_thread_responding"]=true;
    if(world.bMapLoaded) {
        result["zone"]=Text(world.GetJournalZoneName(),256);
        result["map"]=Text(world.szZoneFileName,256);result["mob"]=Text(world.szMobFileName,256);
        result["game_hours"]=world.GetWorldTime();result["game_tick"]=world.GetSegmentTimeCounter();
    } else {result["zone"]=nullptr;result["game_hours"]=nullptr;result["game_tick"]=nullptr;}
    double total=0;for(unsigned i=0;i<perf.liveFrameCount;++i)total+=perf.liveFrames[i];
    result["fps"]=total>0?1000.0*perf.liveFrameCount/total:0.0;
    return result;
}
json Perf() {
    auto& s=CorePerf::Get();json result={{"ok",true},{"frames_ms",json::array()},
        {"top_scopes",json::array()},{"scope_thread","main"},{"scopes_inclusive",true},{"truncated",false}};
    for(unsigned n=0;n<s.liveFrameCount;++n) {
        const unsigned i=(s.liveFrameNext+120-s.liveFrameCount+n)%120;
        result["frames_ms"].push_back(s.liveFrames[i]);
    }
    std::vector<int> ids;ids.reserve(CorePerf::Count);
    for(int i=0;i<CorePerf::Count;++i)if(s.liveScopes[i].calls)ids.push_back(i);
    std::sort(ids.begin(),ids.end(),[&](int a,int b){return s.liveScopes[a].total>s.liveScopes[b].total;});
    for(size_t n=0;n<ids.size()&&n<10;++n) {
        const int i=ids[n];const auto& c=s.liveScopes[i];
        result["top_scopes"].push_back({{"name",CorePerf::Name(CorePerf::Id(i))},
            {"total_ms",1000.0*c.total/s.frequency},{"max_ms",1000.0*c.maximum/s.frequency},{"calls",c.calls}});
    }
    result["scopes_truncated"]=ids.size()>10;
    result["window_ms"]=(CorePerf::Now()-s.liveWindow)*1000.0/s.frequency;
    return result;
}
bool SameWorld(Job& job,CWorldServer& world) {
    return job.world==&world&&job.worldId==world.GetRTID();
}
size_t GetLimit(const json& params,size_t maximum,size_t fallback) {
    if(!params.contains("limit"))return fallback;
    if(!params["limit"].is_number_integer())throw std::runtime_error("limit must be an integer");
    const auto value=params["limit"].get<long long>();
    if(value<1)throw std::runtime_error("limit must be positive");
    return (std::min)(size_t(value),maximum);
}
bool Lod(Job& job,Clock::time_point deadline,CWorldServer& world,const json& params) {
    const auto name=params.value("name",std::string());
    if(name.size()>256)throw std::runtime_error("name filter too long");
    if(params.contains("radius")&&!params["radius"].is_number())throw std::runtime_error("radius must be a number");
    const double radius=params.value("radius",80.0);
    if(!std::isfinite(radius)||radius<0||radius>2000)throw std::runtime_error("radius must be between 0 and 2000 metres");
    const size_t limit=GetLimit(params,200,50);
    if(!job.phase){
        job.world=&world;job.worldId=world.GetRTID();job.phase=1;
        const auto epoch=owned::bridge_current_world_epoch();
        job.lodRequest=owned::native_lod_request(epoch);
        // Leave time for the scene thread/readback, but finish before the pipe's
        // 4.5 s deadline even when no eligible world pass or GPU reply arrives.
        job.lodExpires=Clock::now()+std::chrono::milliseconds(1500);
        job.response={{"ok",true},{"status","waiting"},{"snapshot_frame",nullptr},{"world_epoch",epoch},
            {"objects",json::array()},{"request_id",job.lodRequest},{"limit",limit},{"truncated",false},{"radius",radius},
            {"radius_applied",name.empty()},{"camera_anchor",Point(theCamera.GetAnchorPoint())},
            {"consistency","GPU snapshot; distances from request camera anchor"},
            {"position_basis","GPU render node origin, world metres"},
            {"visibility_basis","colour triangles emitted; not a pixel visibility test"}};
        if(!name.empty()){
            job.response["name"]=name;
            job.response["name_scope"]="active_and_neighbour_snapshot";
            job.response["name_match"]="waiting";
            job.response["model_presence"]="not_determined";
        }
    }
    if(!SameWorld(job,world)||owned::bridge_current_world_epoch()!=job.response["world_epoch"].get<std::uint64_t>()){
        job.response=Error("world changed while awaiting LOD snapshot");return true;
    }
    if(job.phase==1){
        job.lodSnapshot=owned::native_lod_snapshot();
        if(!job.lodSnapshot||job.lodSnapshot->request!=job.lodRequest||
           job.lodSnapshot->world_epoch!=job.response["world_epoch"].get<std::uint64_t>()){
            job.lodSnapshot.reset();
            if(Clock::now()<job.lodExpires)return false;
            job.response["status"]="unavailable";
            job.response["reason"]="lod_snapshot_timeout";
            if(!name.empty())job.response["name_match"]="unavailable";
            return true;
        }
        if(job.lodSnapshot->unavailable){
            job.response["status"]="unavailable";
            job.response["reason"]=job.lodSnapshot->unavailable_reason?
                job.lodSnapshot->unavailable_reason:"gpu_snapshot_unavailable";
            if(job.lodSnapshot->main_camera_max_delta>0)
                job.response["main_camera_max_delta"]=job.lodSnapshot->main_camera_max_delta;
            if(!name.empty())job.response["name_match"]="unavailable";
            return true;
        }
        job.response["snapshot_frame"]=job.lodSnapshot->frame;
        job.response["capture"]={{"camera_frame",job.lodSnapshot->camera_frame},
            {"record_capacity",job.lodSnapshot->record_capacity},{"rows",job.lodSnapshot->rows.size()},
            {"observed_rows",job.lodSnapshot->observed_rows}};
        job.response["status"]="ready";job.response["truncated"]=job.lodSnapshot->truncated;job.phase=2;
        // The snapshot contains submitted render records, not a model catalog.
        // No matching row cannot establish that a loaded/resource model is absent.
        if(!name.empty())job.response["name_match"]="not_in_snapshot";
    }
    if(!SameWorld(job,world)||owned::bridge_current_world_epoch()!=job.lodSnapshot->world_epoch){
        job.response=Error("world changed during LOD scan");return true;
    }
    const auto& rows=job.lodSnapshot->rows;
    const double cameraX=job.response["camera_anchor"][0].get<double>();
    const double cameraY=job.response["camera_anchor"][1].get<double>();
    while(job.item<rows.size()&&Clock::now()<deadline){
        const auto& row=rows[job.item++];const auto& source=*row.identity;const auto& choice=row.decision;
        if(!Match(source.model,name))continue;
        if(!name.empty()){
            job.response["name_match"]="matched";
            job.response["model_presence"]="present";
        }
        // A cleared slot has no GPU sample; no CPU substitute for position or LOD.
        const bool observed=choice.state!=0;
        const double dx=choice.position[0]-cameraX,dy=choice.position[1]-cameraY;
        const double distance=std::sqrt(dx*dx+dy*dy);
        if(name.empty()&&(!observed||distance>radius))continue;
        if(job.response["objects"].size()>=limit){job.response["truncated"]=true;return true;}
        const bool selected=choice.state==2;
        json value={{"id",source.id},{"id_kind",source.candidate_id?"neighbour_candidate":"game_object"},
            {"generation",source.generation},{"part",source.part},{"record",row.record},{"model",source.model},
            {"source",source.neighbour?"neighbour":"active"},
            {"zone",source.neighbour?source.zone:Text(world.GetJournalZoneName(),256)},
            {"map",source.neighbour?source.map:Text(world.szZoneFileName,256)},
            {"mob",source.neighbour?source.mob:Text(world.szMobFileName,256)},
            {"state",!observed?"not_observed":selected?"selected":"culled"},
            {"position",nullptr},{"distance",nullptr},{"rendered",nullptr},{"lod",nullptr},
            {"lod_count",nullptr},{"triangles",nullptr},{"triangles_lod0",nullptr},
            {"merged",nullptr},{"second_lod",nullptr},{"second_triangles",nullptr},{"impostor",false}};
        if(observed){
            value["position"]=choice.position;value["distance"]=distance;
            value["rendered"]=selected&&(choice.first_triangles+choice.second_triangles)>0;
            value["lod_count"]=choice.level_count;value["triangles_lod0"]=choice.full_triangles;
            value["merged"]=bool(choice.merged);
            value["triangles"]=choice.first_triangles+choice.second_triangles;
            if(selected){
                if(choice.first_level!=UINT32_MAX)value["lod"]=choice.first_level;
                if(choice.second_level<UINT32_MAX-1)value["second_lod"]=choice.second_level;
                value["second_triangles"]=choice.second_triangles;
                value["impostor"]=choice.second_level==UINT32_MAX-1;
                value["crossfade_code"]=choice.fade;
            }
        }
        job.response["objects"].push_back(std::move(value));
    }
    return job.item>=rows.size();
}
bool Units(Job& job,Clock::time_point deadline,CWorldServer& world,const json& params) {
    const size_t limit=GetLimit(params,UnitLimit,50);
    if(!job.phase) {
        const bool x=params.contains("x"),y=params.contains("y"),r=params.contains("radius");
        if(x!=y||x!=r)throw std::runtime_error("x, y and radius must be supplied together");
        if(x&&(!params["x"].is_number()||!params["y"].is_number()||!params["radius"].is_number()||params["radius"].get<double>()<0))throw std::runtime_error("invalid radius filter");
        const auto relation=params.value("relation",std::string("all"));
        if(relation!="all"&&relation!="squad"&&relation!="enemies")throw std::runtime_error("invalid relation");
        if(params.value("name",std::string()).size()>256||params.value("prototype",std::string()).size()>256)throw std::runtime_error("filter too long");
        job.world=&world;job.worldId=world.GetRTID();job.bucketCount=world.dynamicObjs.ObserverBucketCount();
        job.initialCount=world.dynamicObjs.GetCount();
        job.response={{"ok",true},{"units",json::array()},{"limit",limit},
                      {"truncated",false},{"consistency","per-frame"}};
        job.phase=1;
    }
    if(!SameWorld(job,world)||job.bucketCount!=world.dynamicObjs.ObserverBucketCount()) {
        job.response["truncated"]=true;job.response["reason"]="world or object index changed during scan";return true;
    }
    if(job.initialCount!=size_t(world.dynamicObjs.GetCount()))job.response["truncated"]=true;
    auto* player=Player();
    const auto relation=params.value("relation",std::string("all"));
    const auto name=params.value("name",std::string()),prototype=params.value("prototype",std::string());
    // Store an ID bookmark, never an iterator or object pointer across frames.
    // Restarting a crowded hash bucket from offset zero can consume the whole
    // budget repeatedly without examining another unit.
    const auto& objects=world.dynamicObjs;
    auto it=job.unitScanStarted?objects.ObserverFind(int(job.unitNextKey)):objects.begin();
    const auto end=objects.end();
    if(job.unitScanStarted&&!job.unitScanFinished&&it==end) {
        job.response["truncated"]=true;job.response["reason"]="object cursor removed during scan";return true;
    }
    while(it!=end&&!job.unitScanFinished&&Clock::now()<deadline) {
        CMapObject* object=it->second;++it;job.unitScanStarted=true;
        job.unitScanFinished=it==end;
        if(!job.unitScanFinished)job.unitNextKey=it->first;
        if(!object||object->IsInvalid()||object->IsUnreferenced()||object->WhoAmI()!=HPC_UNIT)continue;
        auto* unit=static_cast<CUnit*>(object);
        if(!job.seen.insert(unit->GetID()).second)continue;
        if(relation!="all"&&!player)throw std::runtime_error("player relation is unavailable");
        if(relation=="squad"&&!Squad(unit,player))continue;
        if(relation=="enemies") {
            const int number=PlayerNumber(player);if(number<0)throw std::runtime_error("player relation is unavailable");
            if(!Enemy(unit,number))continue;
        }
        const auto& pos=unit->GetCP();
        if(params.contains("x")) {
            const double dx=pos.x-params["x"].get<double>(),dy=pos.y-params["y"].get<double>(),radius=params["radius"].get<double>();
            if(dx*dx+dy*dy>radius*radius)continue;
        }
        const auto* proto=unit->GetPrototype();
        if(!Match(Text(unit->GetName(),256),name)||!Match(proto?Text(proto->szName,256):std::string(),prototype))continue;
        job.response["units"].push_back(UnitRow(unit,world,player));
        if(job.response["units"].size()>=limit){job.response["truncated"]=true;return true;}
    }
    return it==end||job.unitScanFinished;
}

bool Globals(Job& job,Clock::time_point deadline,CWorldServer& world,const json& params) {
    const size_t limit=GetLimit(params,VariableLimit,100);
    const auto prefix=params.value("prefix",std::string());if(prefix.size()>256)throw std::runtime_error("prefix too long");
    if(!job.phase) {
        job.world=&world;job.worldId=world.GetRTID();job.phase=1;
        job.response={{"ok",true},{"variables",json::array()},{"limit",limit},
                      {"truncated",false},{"consistency","per-frame"}};
    }
    if(!SameWorld(job,world)){job.response["truncated"]=true;job.response["reason"]="world changed during scan";return true;}
    auto& vars=world.script.GetGlobalVars();
    auto accept=[&](const std::string& name) {return name.compare(0,prefix.size(),prefix)==0;};
    auto append=[&](const char* scope,const char* type,const std::string& name,json value) {
        if(accept(name))job.response["variables"].push_back({{"scope",scope},{"type",type},{"name",name},{"value",std::move(value)}});
    };
    while(job.phase<=4&&Clock::now()<deadline) {
        const int size=job.phase==1?vars.fVars.GetSize():job.phase==2?vars.szVars.GetSize():job.phase==3?vars.oVars.GetSize():vars.gVars.GetSize();
        if(job.item>=size_t(size)){++job.phase;job.item=0;continue;}
        const int i=int(job.item++);const char* raw=nullptr;
        if(job.phase==1&&vars.fVars.TryGetVarName(i,raw))append("script","float",Text(raw,256),vars.fVars[i]);
        else if(job.phase==2&&vars.szVars.TryGetVarName(i,raw))append("script","string",Text(raw,256),Text(vars.szVars[i],512));
        else if(job.phase==3&&vars.oVars.TryGetVarName(i,raw)) {
            auto* object=vars.oVars[i].GetObject();append("script","object",Text(raw,256),object?object->GetID():-1);
        } else if(job.phase==4&&vars.gVars.TryGetVarName(i,raw)) {
            auto& group=vars.gVars[i];json ids=json::array();
            for(int k=0;k<group.GetSize()&&k<32;++k){auto* object=group.GetAt(k).GetObject();ids.push_back(object?object->GetID():-1);}
            append("script","group",Text(raw,256),{{"ids",std::move(ids)},{"truncated",group.GetSize()>32}});
        }
        if(job.response["variables"].size()>=limit){job.response["truncated"]=true;return true;}
    }
    if(job.phase<=4)return false;
    auto* player=Player();if(!player)return true;
    auto& party=player->party.vars;
    if(job.phase==5){job.bucketCount=party.bucket_count();job.initialCount=party.size();job.phase=6;}
    if(job.bucketCount!=party.bucket_count()){job.response["truncated"]=true;job.response["reason"]="party variables changed during scan";return true;}
    if(job.initialCount!=party.size())job.response["truncated"]=true;
    while(job.bucket<job.bucketCount&&Clock::now()<deadline) {
        auto it=party.cbegin(job.bucket),end=party.cend(job.bucket);size_t i=0;
        while(it!=end&&i<job.offset&&Clock::now()<deadline){++it;++i;}
        if(i<job.offset)return false;
        while(it!=end&&Clock::now()<deadline) {
            append("party","float",Text(it->first.c_str(),256),it->second);++it;++job.offset;
            if(job.response["variables"].size()>=limit){job.response["truncated"]=true;return true;}
        }
        if(it==end){++job.bucket;job.offset=0;}
    }
    return job.bucket>=job.bucketCount;
}
bool Nav(Job& job,Clock::time_point deadline,CWorldServer& world,const json& params,bool path) {
    auto& navigation=world.aiMap.Navigation();
    if(!job.phase) {
        const auto profile=params.value("profile",std::string("human"));int type=-1;
        for(int i=0;i<Navigation::AgentCount;++i)if(profile==Navigation::Agents[i].name)type=i;
        if(type<0)throw std::runtime_error("unknown navigation profile");
        const auto point=[&](const char* x,const char* y,const char* z) {
            const double px=params.at(x).get<double>(),py=params.at(y).get<double>();
            if(!std::isfinite(px)||!std::isfinite(py)||std::abs(px)>100000||std::abs(py)>100000)throw std::runtime_error("invalid coordinates");
            const double pz=params.contains(z)?params[z].get<double>():world.aiMap.GetTileHeight(CPlot3D(float(px),float(py),0));
            if(!std::isfinite(pz)||std::abs(pz)>100000)throw std::runtime_error("invalid height");
            return Navigation::Point{float(px),float(py),float(pz)};
        };
        const auto start=path?point("ax","ay","az"):point("x","y","z");
        const auto goal=path?point("bx","by","bz"):Navigation::Point{};
        if(navigation.ObserverUpdating(type)) {
            job.response={{"ok",true},{"profile",profile},{"status","updating"},{"pending",true},{"truncated",false}};return true;
        }
        job.mesh=navigation.ObserverMesh(type);
        if(!job.mesh){job.response=Error("runtime navigation profile is not loaded");return true;}
        job.world=&world;job.worldId=world.GetRTID();job.navProfile=type;job.navRevision=navigation.ObserverRevision();
        job.navigation=std::make_unique<Navigation::LiveQuery>(start,path?&goal:nullptr,std::move(job.query));job.phase=1;
    }
    if(!SameWorld(job,world)||job.navRevision!=navigation.ObserverRevision()||job.mesh!=navigation.ObserverMesh(job.navProfile)) {
        job.response=Error("runtime mesh changed during query; retry");return true;
    }
    while(Clock::now()<deadline) {
        if(!job.navigation->Step(*job.mesh))continue;
        const auto& result=job.navigation->Get();
        job.response={{"ok",true},{"profile",Navigation::Agents[job.navProfile].name},
            {"status",Navigation::StatusName(result.status)},{"truncated",result.truncated},
            {"dynamic_units_considered",false},{"height_basis","representative profile, not unit body or pose"},
            {"source",Location(result.source)},{"navigation_generation",job.mesh->generation},{"pending",false}};
        if(path) {
            job.response["goal"]=Location(result.goal);job.response["points"]=json::array();
            for(const auto& point:result.points)job.response["points"].push_back(Location(point));
        } else job.response["walkable"]=result.status==Navigation::Status::Complete;
        return true;
    }
    return false;
}
bool Screenshot(Job& job,Clock::time_point deadline) {
    if(!job.phase){job.phase=1;return false;} // Capture at the next completed backbuffer.
    if(job.phase==1||job.phase==6)return false;
    if(job.phase==5) {
        const int status=job.setup.load(std::memory_order_acquire);
        if(status==3)throw std::runtime_error("backbuffer staging allocation failed");
        if(status!=2)return false;
        auto* texture=job.prepared.exchange(nullptr);
        if(!texture)throw std::runtime_error("backbuffer staging allocation lost");
        job.staging.Attach(texture);job.phase=6;return false;
    }
    if(job.phase==4)return true; // CaptureFrame already recorded the error.
    if(job.phase!=2||!job.context||!job.staging)throw std::runtime_error("invalid screenshot state");
    if(!job.isMapped) {
        const HRESULT hr=job.context->Map(job.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&job.mapped);
        if(hr==DXGI_ERROR_WAS_STILL_DRAWING)return false;
        if(FAILED(hr))throw std::runtime_error("backbuffer readback failed");
        job.isMapped=true;
    }
    while(job.row<job.height&&Clock::now()<deadline) {
        std::memcpy(job.image.data()+size_t(job.row)*job.width*4,
                    static_cast<const unsigned char*>(job.mapped.pData)+size_t(job.row)*job.mapped.RowPitch,
                    size_t(job.width)*4);
        ++job.row;
    }
    if(job.row<job.height)return false;
    CleanupCapture(job);job.response={{"ok",true},{"width",job.width},{"height",job.height},{"truncated",false}};return true;
}
std::string CommandName(const json& params) {
    const auto& value=params.at("name");
    if(!value.is_string())throw std::runtime_error("name must be a string");
    const auto name=value.get<std::string>();
    if(name.empty()||name.size()>128||!std::all_of(name.begin(),name.end(),[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';
    }))throw std::runtime_error("name must contain 1..128 ASCII letters, digits, underscores or hyphens");
    return name;
}
CPlayerClient* CommandClient() {
    auto* client=theWorld.GetPlayer();auto* serverPlayer=theWorldServer.GetPlayer(0);
    if(!theWorldServer.bMapLoaded||theWorldServer.GetServerMode()!=SERV_MODE_LOCAL||
       !theWorld.IsConnected()||!client||!serverPlayer)
        throw std::runtime_error("command requires a connected local game");
    auto* server=static_cast<CPlayerServer*>(serverPlayer->GetPlayerSpecific());
    if(!server||(server->GetStatus()!=CPlayerServer::PSS_ACTIVE&&server->GetStatus()!=CPlayerServer::PSS_CAMP)||
       (client->GetStatus()!=CPlayerClient::CPS_ACTIVE&&client->GetStatus()!=CPlayerClient::CPS_CAMP))
        throw std::runtime_error("local player is not ready");
    return client;
}
CPlot3D CommandPoint(const json& params) {
    const auto coordinate=[&](const char* key) {
        if(!params.at(key).is_number())throw std::runtime_error("coordinates must be numbers");
        const auto value=params.at(key).get<double>();
        if(!std::isfinite(value)||std::abs(value)>100000)throw std::runtime_error("invalid coordinates");
        return float(value);
    };
    CPlot3D point(coordinate("x"),coordinate("y"),params.contains("z")?coordinate("z"):0.f);
    if(point.x<0||point.y<0||!theWorldServer.aiMap.TileInBounds(Float2AITile(point.x),Float2AITile(point.y)))
        throw std::runtime_error("point is outside the loaded map");
    return point;
}
bool Command(Job& job) {
    // The first listed command, even if rejected, protects every subsequent
    // slot write in this process. Reading an owner's slot remains permitted.
    saveIsolated=true;
    const auto& method=job.method;const auto& params=job.request.at("params");
    UpdateOperationReadiness();
    {std::lock_guard<std::mutex> lock(operationMutex);
     if(operation.inFlight&&method!="quit")throw std::runtime_error("another game operation is in progress");}
    GameJournal::Source source("mcp");
    if(method=="quit") {
        if(!NGame::CanQuit())throw std::runtime_error("game refused to quit");
        job.quitRequested=true;
        job.response={{"ok",true},{"status","accepted"},{"truncated",false}};
        return true;
    }
    if(method=="load_save"||method=="new_game"||method=="enter_zone") {
        if(IWorldConnector.HasReq())throw std::runtime_error("game already has a pending transition");
        const auto name=method=="new_game"?std::string():CommandName(params);
        if(method=="enter_zone") {
            auto* client=CommandClient();
            if(!theMap.GetZoneByName(name.c_str()))throw std::runtime_error("zone is not in map.txt");
            if(theWorldServer.GetCurrentZoneName().CompareNoCase(name.c_str())==0)
                throw std::runtime_error("already in this zone");
            const auto* player=client->GetPlayer();
            if(player->party.people.GetSize()==0)throw std::runtime_error("player has no party");
            if(theWorldServer.curInterface==IFACE_MISSION||theWorldServer.curInterface==IFACE_BASE) {
                auto* hero=theWorldServer.GetUnit(player->party.people[0].nID);
                if(!hero||hero->IsDead())throw std::runtime_error("main hero cannot leave the zone");
            }
        }
        const auto id=BeginOperation(method,name);job.operationId=id;
        if(method=="load_save")IWorldConnector.ReqLoadGame(name.c_str());
        else if(method=="new_game")IWorldConnector.ReqNewGame();
        else IWorldConnector.ClientGoZone(name.c_str(),0);
        job.response={{"ok",true},{"status","accepted"},{"operation_id",id},{"truncated",false}};
        return true;
    }
    auto* client=CommandClient();
    if(method=="save_game") {
        const auto name=CommandName(params);
        if(name.rfind("mcp_",0)!=0)throw std::runtime_error("save_game requires the mcp_ prefix");
        const auto id=BeginOperation(method,name);job.operationId=id;OperationExecuting("save_game");
        const bool saved=IWorldConnector.ReqSaveGame(name.c_str())!=FALSE;
        OperationApplied("save_game",saved);
        job.response=saved?json{{"ok",true},{"status","done"},{"name",name},{"operation_id",id},{"truncated",false}}:Error("save failed; see journal");
        return true;
    }
    if(method=="god_mode") {
        const auto& value=params.at("level");
        if(!value.is_number_integer()||value<0||value>3)
            throw std::runtime_error("level must be an integer from 0 to 3");
        auto* player=theWorldServer.GetPlayer(0);
        const int previous=player->bGod,level=value.get<int>();
        // The same server-side party player field as SC_GODMODE.
        player->bGod=static_cast<BYTE>(level);
        job.response={{"ok",true},{"status","done"},{"player",0},
            {"previous",previous},{"value",int(player->bGod)},{"truncated",false}};return true;
    }
    if(method=="final_golem_action") {
        // Service-only C19 signature (object, object, operation); not a MOB token.
        for(const char* key:{"boss","other","operation"})if(!params.at(key).is_number_integer())throw std::runtime_error("golem service arguments must be integers");
        const int boss=params.at("boss").get<int>(),other=params.at("other").get<int>(),op=params.at("operation").get<int>();
        ModFinalGolem::Action(theWorldServer.GetUnit(boss),theWorldServer.GetUnit(other),op);
        job.response={{"ok",true},{"status","done"},{"service",true},{"boss",boss},{"other",other},{"operation",op},{"truncated",false}};return true;
    }
    if(method=="script_run") {
        const auto& value=params.at("source");
        if(!value.is_string())throw std::runtime_error("source must be a string");
        const auto source=value.get<std::string>();
        if(source.empty()||source.size()>4096||source.find('\0')!=std::string::npos)
            throw std::runtime_error("source must contain 1..4096 UTF-8 bytes without NUL");
        const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,source.data(),int(source.size()),nullptr,0);
        if(!count)throw std::runtime_error("source must be valid UTF-8");
        std::wstring wide(size_t(count),L'\0');
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,source.data(),int(source.size()),wide.data(),count);
        BOOL substituted=FALSE;
        const int bytes=WideCharToMultiByte(1251,WC_NO_BEST_FIT_CHARS,wide.data(),count,nullptr,0,nullptr,&substituted);
        if(!bytes||substituted)throw std::runtime_error("source must be representable in Windows-1251");
        std::string native(size_t(bytes),'\0');
        WideCharToMultiByte(1251,WC_NO_BEST_FIT_CHARS,wide.data(),count,native.data(),bytes,nullptr,&substituted);
        float result=0;CString error;
        if(!theWorldServer.script.RunFragment(native.c_str(),result,error)) {
            const auto message=Text(error,2048);job.response=Error(message.c_str());job.response["stage"]="parse";
        } else {
            if(!std::isfinite(result))throw std::runtime_error("fragment result is not finite; completed game changes are not rolled back");
            job.response={{"ok",true},{"status","done"},{"service",true},{"result",result},{"truncated",false}};
        }
        return true;
    }
    if(method=="set_global") {
        const auto& key=params.at("name");
        if(!key.is_string())throw std::runtime_error("name must be a string");
        const auto name=key.get<std::string>();
        if(name.empty()||name.size()>128||!std::all_of(name.begin(),name.end(),[](unsigned char c){
            return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';
        }))throw std::runtime_error("global name must contain 1..128 ASCII letters, digits, dots, underscores or hyphens");
        const auto& value=params.at("value");
        if(!value.is_number())throw std::runtime_error("value must be a number");
        const double number=value.get<double>();
        if(!std::isfinite(number)||std::abs(number)>double((std::numeric_limits<float>::max)()))
            throw std::runtime_error("value must be a finite float");
        auto* player=theWorldServer.GetPlayer(0);
        auto* server=static_cast<CPlayerServer*>(player->GetPlayerSpecific());
        const float previous=player->party.vars.GetVar(name.c_str()), stored=float(number);
        // The same party-0 setter as MOB GSSetVar: journal + client PID_SETVAR.
        server->SetVar(name.c_str(),stored);
        job.response={{"ok",true},{"status","done"},{"scope","party"},{"player",0},
            {"name",name},{"previous",previous},{"value",stored},{"truncated",false}};return true;
    }
    if(method=="object_transform") {
        // Service-only entry; future MOB owners call the same typed Nav seam.
        const auto& id=params.at("id");
        if(!id.is_number_integer()||id<0||id>INT_MAX)throw std::runtime_error("invalid scenery id");
        if(!params.contains("z"))throw std::runtime_error("z is required and is terrain-relative");
        const auto point=CommandPoint(params);const auto& angle=params.at("yaw");
        if(!angle.is_number()||!std::isfinite(angle.get<double>())||std::abs(angle.get<double>())>1.e6)
            throw std::runtime_error("yaw must be finite radians");
        if(!params.at("present").is_boolean())throw std::runtime_error("present must be boolean");
        auto& navigation=theWorldServer.aiMap.Navigation();
        navigation.TransformObject(id.get<int>(),{point.x,point.y,point.z},angle.get<float>(),params.at("present").get<bool>());
        job.response={{"ok",true},{"status","queued"},{"service",true},{"id",id},{"position",{point.x,point.y,point.z}},
            {"yaw",angle},{"navigation_present",navigation.ObjectPresent(id.get<int>())},{"truncated",false}};return true;
    }
    if(method=="pause") {
        if(!params.at("on").is_boolean())throw std::runtime_error("on must be boolean");
        IWorld.MissionCombatPause(params.at("on").get<bool>());
        job.response={{"ok",true},{"status","accepted"},{"on",params["on"]},{"truncated",false}};return true;
    }
    const auto& value=params.at("id");
    if(!value.is_number_integer()||value<=0||value>INT_MAX)throw std::runtime_error("invalid unit id");
    const int id=value.get<int>();auto& world=theWorldServer;auto* unit=world.GetUnit(id);
    auto* player=client->GetPlayer();auto* serverPlayer=world.GetPlayer(0);
    if(!unit||unit->IsInvalid()||unit->IsUnreferenced()||!Squad(unit,player)||!Squad(unit,serverPlayer)||
       unit->GetPlayerNum()!=player->GetPlayerNumber())throw std::runtime_error("unit is not in the player's squad");
    if(unit->IsDead()||unit->CheckFlag(UNIT_BLOCKED)||world.curInterface!=IFACE_MISSION||
       player->curInterface!=IFACE_MISSION||serverPlayer->curInterface!=IFACE_MISSION||
       client->GetStatus()!=CPlayerClient::CPS_ACTIVE)throw std::runtime_error("squad unit cannot act in the current state");
    auto* specific=unit->GetUnitSpecific();
    if(!specific||specific->WhoAmI()!=HPC_UNITSERVERSPECIFIC)throw std::runtime_error("unit has no server state");
    auto* server=static_cast<CUnitServer*>(specific);
    if(!server->logic.GetPlayer())throw std::runtime_error("unit does not accept player orders");
    auto point=CommandPoint(params);
    if(method=="unit_move") {
        auto* clientUnit=theWorld.GetUnit(id);
        if(!clientUnit)throw std::runtime_error("unit is not present on the client");
        point.z=world.aiMap.GetTileHeight(point);
        CArray<CUnit*,CUnit*> units;units.Add(clientUnit);
        client->UnitCommandMoveTo(units,point,FALSE);
        job.response={{"ok",true},{"status","accepted"},{"id",id},{"target",Point(point)},
                      {"path","client_to_server"},{"truncated",false}};return true;
    }
    // Service placement, not a path traversal. Keep the normal AI-map placement
    // and Nav64 support coherent; roll back if the requested support is absent.
    const auto previous=unit->GetCP();const auto previousLocation=unit->navLocation;
    if(!world.aiMap.MoveObject(unit,point))throw std::runtime_error("AI map rejected teleport placement");
    unit->navLocation={};
    try {
        if(!world.aiMap.Navigation().Place(unit,world.aiMap))
            throw std::runtime_error("Nav64 rejected teleport placement; original position restored");
    }catch(...) {
        const bool restored=world.aiMap.MoveObject(unit,previous);unit->navLocation=previousLocation;
        if(!restored){ChannelError("teleport_restore_failed",0);throw std::runtime_error("teleport could not restore position; see journal");}
        throw;
    }
    server->logic.GetPlayer()->DoStop(server->logic);server->InitializeUnit();
    unit->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
    job.response={{"ok",true},{"status","done"},{"id",id},{"position",Point(unit->GetCP())},
        {"navigation_position",Point(unit->navLocation.position)},{"service_action",true},
        {"navigation_traversal",false},{"z_kind","ground_relative"},{"truncated",false}};return true;
}
bool Step(Job& job,Clock::time_point deadline) {
    const auto& method=job.method;const auto& params=job.request.at("params");
    if(IsCommand(method))return Command(job);
    if(method=="status"){job.response=Status();return true;}
    if(method=="perf"){job.response=Perf();return true;}
    if(method=="screenshot")return Screenshot(job,deadline);
    auto& world=theWorldServer;
    if(!world.bMapLoaded){job.response=Error("no loaded game world");return true;}
    if(method=="unit") {
        const int id=params.at("id").get<int>();auto* unit=world.GetUnit(id);
        if(!unit||unit->IsInvalid()||unit->IsUnreferenced()){job.response=Error("unit not found");return true;}
        auto detail=UnitDetail(unit,world,Player());
        const bool truncated=detail.value("effects_truncated",false)||
            (detail.contains("navigation")&&detail["navigation"].value("truncated",false));
        job.response={{"ok",true},{"unit",std::move(detail)},{"world_epoch",owned::bridge_current_world_epoch()},
                      {"truncated",truncated}};return true;
    }
    if(method=="lod")return Lod(job,deadline,world,params);
    if(method=="units")return Units(job,deadline,world,params);
    if(method=="globals")return Globals(job,deadline,world,params);
    if(method=="nav"||method=="nav_path")return Nav(job,deadline,world,params,method=="nav_path");
    job.response=Error("unknown live method");return true;
}
} // namespace
} // namespace LiveObserver
