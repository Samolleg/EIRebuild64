#include "gltf_animation.h"
#include "native_shader.h"
#include "gltf_playback.h"
#include "model_preview_clock.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <iostream>
#include <functional>
#ifdef CHECK_GLTF_ASSET
#include "gltf_asset.h"
#include <filesystem>
#include <fstream>
#endif
using namespace owned::gltf;
using Microsoft::WRL::ComPtr;
static void check(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
static void hr(HRESULT r){check(SUCCEEDED(r),"D3D11 operation");}
static void check_near(float a,float b,const char* reason){if(std::abs(a-b)>5e-5f){std::cerr<<a<<" != "<<b<<'\n';throw std::runtime_error(reason);}}
static void rejects(const std::function<void()>& run){bool failed=false;try{run();}catch(const std::runtime_error&){failed=true;}check(failed,"malformed input accepted");}
struct PoseGpu {
    unsigned asset=0,output=0,frame=0;float fraction=0,shape[3]{},height=1,center[3]{};unsigned padding=0;
    Affine root{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};unsigned generation[2]{1,2};
    unsigned clip=0,previous=UINT32_MAX;float time=0,previousTime=0,blend=1;
};
struct NodeGpu {Float4 rotation,translation,rows[3];unsigned first,second,count;float fraction;unsigned generation[2];};
static_assert(sizeof(PoseGpu)==140&&sizeof(NodeGpu)==104);
static std::vector<NodeGpu> gpu12(const Animation& a,const Playback& playback){
    auto packed=pack(a,"test12");const unsigned base=2,total=base+packed->nodes+packed->morphs;
    // Library precedes two independent headers. Exercise uint32-relative
    // addressing across the wrap boundary without changing the shader ABI.
    const unsigned skeleton=unsigned((packed->bytes.size()+15)&~std::size_t(15));
    std::vector<unsigned char> bytes(skeleton+64);unsigned header[8]{0,0,0,0,0,0,0u-skeleton,base};
    std::memcpy(bytes.data(),packed->bytes.data(),packed->bytes.size());
    std::memcpy(bytes.data()+skeleton,header,32);header[6]=0u-(skeleton+32);std::memcpy(bytes.data()+skeleton+32,header,32);
    ComPtr<IDXGIFactory4> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;
    hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));ComPtr<ID3D12Device> device;
    hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_ROOT_PARAMETER parameters[4]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[0].Descriptor.ShaderRegister=10;
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[1].Descriptor.ShaderRegister=11;
    parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;parameters[2].Descriptor.ShaderRegister=4;
    parameters[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[3].Constants.ShaderRegister=4;parameters[3].Constants.Num32BitValues=4;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.NumParameters=4;rootDesc.pParameters=parameters;
    ComPtr<ID3DBlob> rootBytes,error;hr(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&rootBytes,&error));
    ComPtr<ID3D12RootSignature> root;hr(device->CreateRootSignature(0,rootBytes->GetBufferPointer(),rootBytes->GetBufferSize(),IID_PPV_ARGS(&root)));
    static ComPtr<ID3DBlob> code;
    if(!code){auto source=owned::compose_native_shader_source();hr(D3DCompile(source.data(),source.size(),nullptr,nullptr,nullptr,"nativePoseCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error));}
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};pipelineDesc.pRootSignature=root.Get();pipelineDesc.CS={code->GetBufferPointer(),code->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;hr(device->CreateComputePipelineState(&pipelineDesc,IID_PPV_ARGS(&pipeline)));
    const auto buffer=[&](UINT64 size,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags,const void* data){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;
        d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;
        ComPtr<ID3D12Resource> resource;hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&resource)));
        if(data){void* p=nullptr;D3D12_RANGE empty{0,0};hr(resource->Map(0,&empty,&p));std::memcpy(p,data,std::size_t(size));resource->Unmap(0,nullptr);}return resource;};
    PoseGpu input;input.asset=skeleton+32;input.clip=playback.clip;input.previous=playback.previous;input.time=playback.time;input.previousTime=playback.previous_time;input.blend=playback.blend;
    auto animation=buffer(bytes.size(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE,bytes.data());
    auto poses=buffer(sizeof(input),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE,&input);
    auto output=buffer(total*sizeof(NodeGpu),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,nullptr);
    auto read=buffer(total*sizeof(NodeGpu),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_NONE,nullptr);
    D3D12_COMMAND_QUEUE_DESC queueDesc{};queueDesc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ComPtr<ID3D12CommandQueue> queue;
    hr(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));ComPtr<ID3D12CommandAllocator> allocator;
    hr(device->CreateCommandAllocator(queueDesc.Type,IID_PPV_ARGS(&allocator)));ComPtr<ID3D12GraphicsCommandList> commands;
    hr(device->CreateCommandList(0,queueDesc.Type,allocator.Get(),pipeline.Get(),IID_PPV_ARGS(&commands)));
    commands->SetComputeRootSignature(root.Get());commands->SetComputeRootShaderResourceView(0,animation->GetGPUVirtualAddress());
    commands->SetComputeRootShaderResourceView(1,poses->GetGPUVirtualAddress());commands->SetComputeRootUnorderedAccessView(2,output->GetGPUVirtualAddress());
    const unsigned work[4]{0,1,0,0};commands->SetComputeRoot32BitConstants(3,4,work,0);commands->Dispatch(1,1,1);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=output.Get();
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;commands->ResourceBarrier(1,&barrier);
    commands->CopyResource(read.Get(),output.Get());hr(commands->Close());ID3D12CommandList* lists[]{commands.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));hr(queue->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(event!=nullptr,"D3D12 fence event");
    const auto waitResult=fence->SetEventOnCompletion(1,event);if(FAILED(waitResult)){CloseHandle(event);hr(waitResult);}
    const auto waited=WaitForSingleObject(event,60000);CloseHandle(event);check(waited==WAIT_OBJECT_0,"D3D12 readback timeout");hr(device->GetDeviceRemovedReason());
    void* mapped=nullptr;hr(read->Map(0,nullptr,&mapped));auto* first=static_cast<const NodeGpu*>(mapped)+base;
    std::vector<NodeGpu> result(first,first+total-base);D3D12_RANGE empty{0,0};read->Unmap(0,&empty);return result;
}
static std::vector<NodeGpu> gpu(const Animation& a,const Playback& playback){
    auto packed=pack(a,"test");const unsigned base=2,total=base+packed->nodes+packed->morphs;
    const unsigned skeleton=unsigned((packed->bytes.size()+15)&~std::size_t(15));
    std::vector<unsigned char> bytes(skeleton+64);
    unsigned header[8]{0,0,0,0,0,0,0u-skeleton,base};std::memcpy(bytes.data(),packed->bytes.data(),packed->bytes.size());
    std::memcpy(bytes.data()+skeleton,header,32);header[6]=0u-(skeleton+32);std::memcpy(bytes.data()+skeleton+32,header,32);
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context));
    static ComPtr<ID3DBlob> code;
    if(!code){const auto source=owned::compose_native_shader_source();ComPtr<ID3DBlob> error;
        const auto result=D3DCompile(source.data(),source.size(),nullptr,nullptr,nullptr,"nativePoseCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(error)std::cerr<<static_cast<const char*>(error->GetBufferPointer());hr(result);}
    ComPtr<ID3D11ComputeShader> shader;hr(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=unsigned(bytes.size());desc.Usage=D3D11_USAGE_IMMUTABLE;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    D3D11_SUBRESOURCE_DATA init{bytes.data(),0,0};ComPtr<ID3D11Buffer> raw;hr(device->CreateBuffer(&desc,&init,&raw));
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R32_TYPELESS;sd.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;
    sd.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;sd.BufferEx.NumElements=desc.ByteWidth/4;
    ComPtr<ID3D11ShaderResourceView> rawView;hr(device->CreateShaderResourceView(raw.Get(),&sd,&rawView));
    PoseGpu input;input.asset=skeleton;input.clip=playback.clip;input.previous=playback.previous;input.time=playback.time;input.previousTime=playback.previous_time;input.blend=playback.blend;
    desc.ByteWidth=sizeof(input);desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=sizeof(input);
    init.pSysMem=&input;ComPtr<ID3D11Buffer> poses;hr(device->CreateBuffer(&desc,&init,&poses));
    ComPtr<ID3D11ShaderResourceView> poseView;hr(device->CreateShaderResourceView(poses.Get(),nullptr,&poseView));
    desc.ByteWidth=total*sizeof(NodeGpu);desc.StructureByteStride=sizeof(NodeGpu);desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Buffer> output;hr(device->CreateBuffer(&desc,nullptr,&output));
    ComPtr<ID3D11UnorderedAccessView> outputView;hr(device->CreateUnorderedAccessView(output.Get(),nullptr,&outputView));
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.Usage=D3D11_USAGE_IMMUTABLE;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    unsigned work[4]{0,1,0,0};D3D11_SUBRESOURCE_DATA cbInit{work,0,0};ComPtr<ID3D11Buffer> constants;
    hr(device->CreateBuffer(&cb,&cbInit,&constants));context->CSSetConstantBuffers(4,1,constants.GetAddressOf());
    context->CSSetShader(shader.Get(),nullptr,0);context->CSSetShaderResources(10,1,rawView.GetAddressOf());context->CSSetShaderResources(11,1,poseView.GetAddressOf());
    context->CSSetUnorderedAccessViews(4,1,outputView.GetAddressOf(),nullptr);context->Dispatch(1,1,1);
    ID3D11UnorderedAccessView* empty=nullptr;context->CSSetUnorderedAccessViews(4,1,&empty,nullptr);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;
    ComPtr<ID3D11Buffer> read;hr(device->CreateBuffer(&desc,nullptr,&read));context->CopyResource(read.Get(),output.Get());
    D3D11_MAPPED_SUBRESOURCE map{};hr(context->Map(read.Get(),0,D3D11_MAP_READ,0,&map));
    auto* first=static_cast<const NodeGpu*>(map.pData)+base;std::vector<NodeGpu> values(first,first+total-base);context->Unmap(read.Get(),0);
    // Execute the production vertex skinning function with an animated morph.
    // Rotation and nonuniform scale make post-skin addition observably wrong.
    if(!a.morphs.empty()){
        static ComPtr<ID3DBlob> skinCode;
        if(!skinCode){auto source=owned::compose_native_shader_source();source+=R"(
[numthreads(1,1,1)]void checkSkin(){
 NativeInstance instance=(NativeInstance)0;
 instance.objectToWorld=float4x4(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1);
 instance.geometryOffsets=uint4(0,64,128,144);instance.colorOffsets=0xffffffffu;
 instance.shapeCount=1;instance.terrainVertexOffset=0xffffffffu;instance.poseNode=0;
 NativeWorldVertex v=prepareNativeWorldVertex(instance,144);
 NativePoseNode result=(NativePoseNode)0;result.row0=v.p;result.row1=v.normal;result.row2=v.tangent;
 nativePoseOutput[0]=result;
})";ComPtr<ID3DBlob> error;auto result=D3DCompile(source.data(),source.size(),nullptr,nullptr,nullptr,"checkSkin","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&skinCode,&error);
            if(error)std::cerr<<static_cast<const char*>(error->GetBufferPointer());hr(result);}
        ComPtr<ID3D11ComputeShader> skinShader;hr(device->CreateComputeShader(skinCode->GetBufferPointer(),skinCode->GetBufferSize(),nullptr,&skinShader));
        std::vector<unsigned char> geometry(416);
        const auto put=[&](unsigned offset,const void* data,unsigned size){std::memcpy(geometry.data()+offset,data,size);};
        const float position[3]{1,.5f,-.25f},normal[3]{0,1,0};
        for(unsigned c=0;c<3;++c){put(c*16,position+c,4);put(64+c*16,normal+c,4);}
        const unsigned component[4]{0,0x80000000,0,160},joint=base+1,correction=0x80000000u|(base+packed->nodes);
        const float weight=1,tangent[4]{1,0,0,1},inverse[12]{1,0,0,0,0,1,0,0,0,0,1,0};
        put(144,component,16);put(160,&joint,4);put(176,&weight,4);put(192,inverse,48);put(384,tangent,16);put(400,&correction,4);
        D3D11_BUFFER_DESC gd{};gd.ByteWidth=unsigned(geometry.size());gd.Usage=D3D11_USAGE_IMMUTABLE;gd.BindFlags=D3D11_BIND_SHADER_RESOURCE;gd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        init.pSysMem=geometry.data();ComPtr<ID3D11Buffer> geometryBuffer;hr(device->CreateBuffer(&gd,&init,&geometryBuffer));
        sd.BufferEx.NumElements=gd.ByteWidth/4;ComPtr<ID3D11ShaderResourceView> geometryView;hr(device->CreateShaderResourceView(geometryBuffer.Get(),&sd,&geometryView));
        ComPtr<ID3D11ShaderResourceView> jointsView;hr(device->CreateShaderResourceView(output.Get(),nullptr,&jointsView));
        D3D11_BUFFER_DESC rd{};rd.ByteWidth=sizeof(NodeGpu);rd.Usage=D3D11_USAGE_DEFAULT;rd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        rd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;rd.StructureByteStride=sizeof(NodeGpu);
        ComPtr<ID3D11Buffer> skinResult;hr(device->CreateBuffer(&rd,nullptr,&skinResult));ComPtr<ID3D11UnorderedAccessView> skinView;
        hr(device->CreateUnorderedAccessView(skinResult.Get(),nullptr,&skinView));
        context->CSSetShader(skinShader.Get(),nullptr,0);context->CSSetShaderResources(1,1,geometryView.GetAddressOf());context->CSSetShaderResources(12,1,jointsView.GetAddressOf());
        context->CSSetUnorderedAccessViews(4,1,skinView.GetAddressOf(),nullptr);context->Dispatch(1,1,1);context->CSSetUnorderedAccessViews(4,1,&empty,nullptr);
        rd.Usage=D3D11_USAGE_STAGING;rd.BindFlags=0;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;rd.MiscFlags=0;rd.StructureByteStride=0;
        ComPtr<ID3D11Buffer> skinRead;hr(device->CreateBuffer(&rd,nullptr,&skinRead));context->CopyResource(skinRead.Get(),skinResult.Get());
        hr(context->Map(skinRead.Get(),0,D3D11_MAP_READ,0,&map));NodeGpu skin;std::memcpy(&skin,map.pData,sizeof(skin));context->Unmap(skinRead.Get(),0);
        const auto expected=evaluate(a,playback);const auto& matrix=expected.joints[1];
        for(unsigned r=0;r<3;++r){float value=matrix[r*4+3];for(unsigned c=0;c<3;++c)value+=matrix[r*4+c]*(position[c]+expected.morphs[0][c]);
            check_near(skin.rows[0][r],value,"production morph-before-skin position");}
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)check(std::isfinite(skin.rows[r][c]),"nonfinite skinned basis");
    }
    return values;
}
int wmain(int argc,wchar_t** argv)try{
    {
        auto database=std::make_shared<owned::AnimationDatabaseAsset>();database->entries.resize(2);
        database->entries[0].length=10;database->entries[0].flags=0x80180000;
        database->entries[1].length=8;database->entries[1].flags=0x00180000;
        auto clip=std::make_shared<owned::AnimationClipAsset>();
        owned::VisualPoseIntent intent;intent.revision=1;intent.resource_source_generation=1;
        intent.selections.push_back({database,clip,0,false});
        owned::VisualPoseState anchor,pose;const char* failure=nullptr;
        check(owned::anchor_visual_pose(intent,{},anchor,&failure),"host phase anchor");
        owned::VisualRateValues rates;rates.dont_move=true;owned::VisualRouteSample route;
        check(owned::sample_visual_pose(intent,anchor,rates,route,25,pose,&failure),"host loop sample");
        check(pose.cycle==2&&pose.phase==5,"host loop count");
        check(owned::sample_visual_pose(intent,anchor,rates,route,30,pose,&failure),"host exact endpoint");
        check(pose.cycle==2&&pose.phase==10,"legacy exact endpoint changed");
        anchor=pose;check(owned::sample_visual_pose(intent,anchor,rates,route,30.5,pose,&failure),"host continued sample");
        check(pose.cycle==3&&pose.phase==.5,"host cycle accumulation");
        rates.frozen=true;anchor=pose;check(owned::sample_visual_pose(intent,anchor,rates,route,100,pose,&failure),"host freeze");
        check(pose.cycle==3&&pose.phase==.5,"frozen phase moved");rates.frozen=false;
        intent.selections.push_back({database,clip,1,false});intent.phase=25;
        check(owned::anchor_visual_pose(intent,{},anchor,&failure),"queued anchor");
        check(anchor.selection==1&&anchor.cycle==0&&anchor.phase==8,"queue end clamp/reset");
        owned::ModelPreviewView view;owned::RetainedVisualSample preview;preview.selection={database,clip,0,false};
        check(owned::prepare_model_preview_phase(view,preview,1,1,0,0,1),"preview anchor");
        check(owned::prepare_model_preview_phase(view,preview,1,2,25,0,1),"preview loop");
        check(preview.pose.cycle==2&&preview.pose.phase==5,"preview cycle/phase");
        const auto before=view.last_time;
        check(!owned::prepare_model_preview_phase(view,preview,1,3,1e30,0,1),"preview overflowing cycles accepted");
        check(view.last_time==before,"rejected preview changed anchor");
        std::cout<<"PASS host ADB loop/exact endpoint/queue/freeze and private preview clock regression\n";
    }
    Animation a;a.nodes.resize(3);a.nodes[1].parent=0;a.nodes[1].translation={0,2,0,0};a.nodes[2].weights={.25f,.5f};
    Clip c;c.name="idle";c.duration=2;
    c.tracks.push_back({0,3,Path::translation,Interpolation::linear,{0,.7f,2},{0,0,0, 1,2,3, 2,0,1}});
    c.tracks.push_back({0,4,Path::rotation,Interpolation::linear,{0,2},{0,0,0,1, 0,0,-.70710678f,-.70710678f}});
    c.tracks.push_back({1,3,Path::scale,Interpolation::step,{0,1,2},{1,1,1, 2,3,4, 1,2,3}});
    c.tracks.push_back({2,2,Path::weights,Interpolation::cubic,{0,2},{0,0, .25f,.5f, .5f,-.25f, -.2f,.3f, 1,0, 0,0}});
    Clip b;b.name="walk";b.duration=2;
    b.tracks.push_back({0,3,Path::translation,Interpolation::cubic,{0,2},{0,0,0, 0,0,0, 2,0,0, 0,0,0, 4,0,0, 0,0,0}});
    b.tracks.push_back({1,4,Path::rotation,Interpolation::cubic,{0,2},{0,0,0,0, 0,0,0,1, 0,.2f,0,0, 0,0,0,0, 0,1,0,0, 0,0,0,0}});
    a.clips={c,b};a.morphs.push_back({2,{{1,2,3,.1f,.2f,.3f,0,1,0},{-2,0,1,-.1f,0,.2f,1,0,0}}});
    validate(a);
    check_near(sample(c.tracks[0],.35f)[0],.5f,"linear analytic");
    check_near(sample(b.tracks[0],1)[0],2.5f,"cubic dt-scaled tangent");
    check_near(sample(c.tracks[2],.999f)[0],1,"step before boundary");check_near(sample(c.tracks[2],1)[0],2,"step boundary");
    const auto q=sample(c.tracks[1],1);check_near(q[2],std::sin(3.14159265f/8),"shortest path rotation");
    for(const Playback p:std::vector<Playback>{{0,UINT32_MAX,-1,0,1},{0,UINT32_MAX,.35f,0,1},{0,UINT32_MAX,1.3f,0,1},
        {1,UINT32_MAX,1,0,1},{0,1,.4f,1.2f,.37f},{UINT32_MAX,UINT32_MAX,0,0,1},{1,UINT32_MAX,3,0,1}}){
        const auto expected=evaluate(a,p);const auto actual=gpu(a,p);
        const auto actual12=gpu12(a,p);
        for(unsigned i=0;i<a.nodes.size();++i)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
            check_near(actual[i].rows[r][c],expected.joints[i][r*4+c],"CPU/GPU joint mismatch");
        for(unsigned i=0;i<actual.size();++i)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
            check_near(actual12[i].rows[r][c],actual[i].rows[r][c],"D3D11/D3D12 pose/morph mismatch");
        for(unsigned i=0;i<a.morphs.size();++i)for(unsigned k=0;k<9;++k)
            check_near(actual[a.nodes.size()+i].rows[k/3][k%3],expected.morphs[i][k],"CPU/GPU morph mismatch");
        for(const auto& n:actual)check(n.generation[0]==1&&n.generation[1]==2,"generation stamp");
    }
    std::cout<<"PASS D3D11/D3D12 WARP readback: STEP/LINEAR/CUBICSPLINE, quaternion, hierarchy/scale, morphs, blend/defaults/endpoints\n";
    auto bad=a;bad.clips[0].tracks[0].times[1]=0;rejects([&]{validate(bad);});bad=a;bad.nodes[1].parent=1;rejects([&]{validate(bad);});
    bad=a;bad.clips[0].tracks.push_back(bad.clips[0].tracks[0]);rejects([&]{validate(bad);});bad=a;bad.clips[0].tracks[0].values[0]=NAN;rejects([&]{validate(bad);});
    bad=a;bad.nodes[0].scale[0]=0;rejects([&]{validate(bad);});
    bad=a;auto& rotation=bad.clips[1].tracks[1];rotation.values={0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,0,0};rejects([&]{validate(bad);});
    std::cout<<"PASS malformed time/hierarchy/duplicate channel/nonfinite/singular scale/zero cubic quaternion rejection\n";
    Request request;request.legacy="cidle01";request.flags=0x80180000;request.frames=30;
    check(resolve_clip(a,request)==0,"base idle");request.flags=0x80140000;check(resolve_clip(a,request)==1,"base walk");
    a.bindings={{"cidle01",0,0},{"",0x80140000,1}};check(resolve_clip(a,request)==0,"legacy priority");
    request.legacy="walk";check(resolve_clip(a,request)==1,"tuple priority");request.flags|=1;check(resolve_clip(a,request)==1,"no weapon tuple");
    request.flags=0x80188000;rejects([&]{resolve_clip(a,request);});request.flags=0x00080000;rejects([&]{resolve_clip(a,request);});
    request.flags=0x80140000;check_near(clip_time(c,request,45),1,"cyclic phase");request.flags=0x00140000;
    check_near(clip_time(c,request,45),2,"unique clamps");
    Clip events;events.name="attack";events.duration=2;events.events={{"hit1","hit","{}",1},{"end","action_end","{}",2}};
    request.flags=0x00080000;request.event_flags=4;request.hit=15;validate_event_phase(events,request);
    request.hit=20;rejects([&]{validate_event_phase(events,request);});request.hit=15;request.flags=0x000c0000;rejects([&]{validate_event_phase(events,request);});
    EventCursor cursor;std::vector<std::string> delivered;
    const auto receive=[&](std::uint64_t cycle,const Event& e){delivered.push_back(std::to_string(cycle)+e.id);};
    ActionToken token{1,0,0};cursor.Advance(events,token,0,true,false,receive);cursor.Advance(events,token,.6,true,false,receive);
    cursor.Advance(events,token,.6,true,false,receive);check(delivered.size()==1,"duplicate event on same sample");
    cursor.Advance(events,token,2.6,true,false,receive);check(delivered==std::vector<std::string>{"0hit1","0end","1hit1","1end","2hit1"},"events across loops");
    cursor.Advance(events,token,.2,true,true,receive);cursor.Advance(events,token,.2,true,false,receive);check(delivered.size()==5,"seek replayed history");
    ++token.intent;cursor.Advance(events,token,.6,false,false,receive);check(delivered.size()==6,"new action event");
    cursor.Advance(events,token,5,false,false,receive);cursor.Advance(events,token,8,false,false,receive);check(delivered.size()==7,"unique end delivered twice");
    rejects([&]{cursor.Advance(events,token,1e30,true,false,receive);});
    PlaybackState state;token={1,0,0};auto play=state.Update(0,.3f,token,0,false);check(play.blend==1,"initial fade");
    ++token.intent;play=state.Update(1,0,token,1,false);check(play.previous==0&&play.blend==0,"transition source");
    play=state.Update(1,.2f,token,1.06,false);check_near(play.blend,.5f,"transition duration");
    ++token.intent;play=state.Update(0,0,token,1.07,false);check(play.blend==1,"interrupted transition");
    ++token.intent;play=state.Update(1,0,token,2,false,true);check(play.blend==1,"terminal transition");
    play=state.Update(1,1,token,1,true);check(play.blend==1&&play.previous==UINT32_MAX,"seek transition");
    const auto bounds=envelope(a);for(const auto& clip:a.clips)for(unsigned step=0;step<=100;++step){auto local=sample_local(a,named_clip(a,clip.name),clip.duration*step/100);
        auto pose=evaluate(a,{named_clip(a,clip.name),UINT32_MAX,clip.duration*step/100,0,1});
        for(unsigned i=0;i<a.nodes.size();++i){double distance=0;for(unsigned c=0;c<3;++c)distance+=double(pose.joints[i][c*4+3])*pose.joints[i][c*4+3];
            check(std::sqrt(distance)<=bounds[i].translation+1e-5,"animated translation outside envelope");
            for(unsigned c=0;c<local[i].weights.size();++c)check(std::abs(local[i].weights[c])<=bounds[i].weights[c]+1e-5,"weight outside envelope");}}
    std::cout<<"PASS bindings, loop/clamp, event phases/deduplication/seek, transitions, cubic motion bounds\n";
#ifdef CHECK_GLTF_ASSET
    if(argc==3){
        const auto read=[](const wchar_t* path){std::ifstream f(std::filesystem::path(path),std::ios::binary|std::ios::ate);
            check(bool(f),"asset open");const auto size=f.tellg();check(size>0&&size<=512*1024*1024,"asset size");
            owned::ResourceBlob blob;auto bytes=std::make_shared<std::vector<std::uint8_t>>(std::size_t(size));f.seekg(0);
            check(bool(f.read(reinterpret_cast<char*>(bytes->data()),size)),"asset read");blob.bytes=bytes;return blob;};
        const auto asset=decode(read(argv[1]),read(argv[2]),[](const std::string&,const void* p,std::uint32_t n){
            owned::ResourceBlob b;const auto* bytes=static_cast<const std::uint8_t*>(p);b.bytes=std::make_shared<std::vector<std::uint8_t>>(bytes,bytes+n);return b;});
        const auto& animation=*asset->animation;const auto index=named_clip(animation,"idle");check(index!=UINT32_MAX,"asset idle");
        float maxError=0;
        for(float fraction:{0.f,.37f,1.f}){const Playback p{index,UINT32_MAX,animation.clips[index].duration*fraction,0,1};
            const auto actual=gpu(animation,p);const auto expected=evaluate(animation,p).joints;
            const auto actual12=gpu12(animation,p);
            for(unsigned i=0;i<animation.nodes.size();++i)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c){
                maxError=(std::max)(maxError,std::abs(actual[i].rows[r][c]-expected[i][r*4+c]));
                check_near(actual[i].rows[r][c],expected[i][r*4+c],"real GLB CPU/GPU matrix");
                check_near(actual12[i].rows[r][c],expected[i][r*4+c],"real GLB CPU/D3D12 matrix");}}
        std::cout<<"PASS actual GLB: "<<animation.nodes.size()<<" nodes, 3 idle samples, max matrix error "<<maxError<<'\n';
    }
#endif
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
