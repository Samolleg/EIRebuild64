// Runs the actual RT repack/probe shaders on synthetic buffers; no game or swap chain.
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <string>
#include <array>
#include "rt-cpu-layout.inc"
using Microsoft::WRL::ComPtr;
void hr(HRESULT r){if(FAILED(r))throw std::runtime_error("HRESULT "+std::to_string(unsigned(r)));}
int run(const std::string& shaderPath, bool hardware, bool probe, bool posed = true) {
 std::ifstream f(shaderPath,std::ios::binary);std::vector<char> shader((std::istreambuf_iterator<char>(f)),{});
 ComPtr<IDXGIFactory4> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
 ComPtr<IDXGIAdapter> adapter;
 if(hardware)hr(factory->EnumAdapters(0,&adapter));else hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
 ComPtr<ID3D12Device> dev;hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
 D3D12_DESCRIPTOR_RANGE ranges[4]{};
 ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,40,0,0};
 ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,60,0,1};
 ranges[2]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,65,0,2};
 ranges[3]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,3};
 D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
 params[0].DescriptorTable={4,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
 params[1].Constants={12,0,4};D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;
 ComPtr<ID3DBlob> bytes,error;hr(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&bytes,&error));
 ComPtr<ID3D12RootSignature> root;hr(dev->CreateRootSignature(0,bytes->GetBufferPointer(),bytes->GetBufferSize(),IID_PPV_ARGS(&root)));
 D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={shader.data(),shader.size()};
 ComPtr<ID3D12PipelineState> pso;hr(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
 auto buffer=[&](UINT64 size,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,const void* data){
  D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;
  d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(type==D3D12_HEAP_TYPE_DEFAULT)d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> r;hr(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));
  if(data){void* p;hr(r->Map(0,nullptr,&p));std::memcpy(p,data,size);r->Unmap(0,nullptr);}return r;};
 NativeWorldVertexGpu vertices[8]{};NativeWorldStateGpu states[4]{};
 for(unsigned i=0;i<8;++i){vertices[i].position[0]=100.f*i+1;vertices[i].position[1]=100.f*i+2;vertices[i].position[2]=100.f*i+3;vertices[i].position[3]=1;
  vertices[i].texture_coordinate[0]=float(i)+.125f;vertices[i].texture_coordinate[1]=float(i)+.375f;for(auto& t:vertices[i].tangent)t=-999.f;}
 for(unsigned i=0;i<4;++i){states[i].geometry={1000*i+1,1000*i+2};states[i].pose={7000*i+3,7000*i+4};states[i].skn_lod=55;states[i].preparations=99;states[i].components=777;}
 unsigned probes[4][4]={{0,1,2,0},{1,1001,1002,0},{3,999,999,0},{UINT_MAX,1,2,0}};
 // A different generation must stay rejected even with a valid state index.
 RaytracingRepackJob job{};job.row[0][0]=job.row[1][1]=job.row[2][2]=1;
 job.position_offset=0;job.uv_offset=64;job.component_offset=96;job.index_offset=160;
 job.shape_count=1;job.position_groups=1;job.component_count=4;job.triangle_count=2;
 job.vertex_base=1;job.triangle_base=1;job.uv_shift[0]=.25f;job.uv_shift[1]=-.5f;
 job.terrain_vertex_offset=UINT_MAX;job.padding[0]=posed?2u:UINT_MAX;
 unsigned page[46]{};for(unsigned i=0;i<4;++i){page[24+i*4]=i;page[26+i*4]=i;}
 float packed[16]{};float uvInput[8]{};
 for(unsigned i=0;i<4;++i){packed[i]=-10.f-float(i);packed[4+i]=20.f+float(i);packed[8+i]=30.f+float(i);packed[12+i]=1;uvInput[i*2]=float(i)+.125f;uvInput[i*2+1]=float(i)+.375f;}
 std::memcpy(page,packed,sizeof(packed));std::memcpy(page+16,uvInput,sizeof(uvInput));
 const unsigned triangles[6]={0,1,2,1,2,3};std::memcpy(page+40,triangles,sizeof(triangles));
 auto input=buffer(probe?sizeof(probes):sizeof(job),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,probe?static_cast<const void*>(probes):&job);
 auto source=buffer(probe?sizeof(states):sizeof(page),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,probe?static_cast<const void*>(states):page);
 auto world=buffer(sizeof(vertices),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,vertices);
 auto out=buffer(160,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr);
 auto indices=buffer(48,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr);
 auto read=buffer(208,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,nullptr);
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=5;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
 ComPtr<ID3D12DescriptorHeap> heap;hr(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
 auto handle=heap->GetCPUDescriptorHandleForHeapStart();auto step=dev->GetDescriptorHandleIncrementSize(hd.Type);
 ID3D12Resource* sources[]={input.Get(),source.Get(),world.Get()};
 const unsigned strides[]={probe?16u:unsigned(sizeof(job)),probe?unsigned(sizeof(NativeWorldStateGpu)):0u,unsigned(sizeof(NativeWorldVertexGpu))};
 const unsigned counts[]={probe?4u:1u,probe?4u:46u,8u};
 for(int i=0;i<3;++i){D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=counts[i];view.Buffer.StructureByteStride=strides[i];
  if(!strides[i]){view.Format=DXGI_FORMAT_R32_TYPELESS;view.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;}
  dev->CreateShaderResourceView(sources[i],&view,handle);handle.ptr+=step;}
 for(int i=0;i<2;++i){D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uv.Format=DXGI_FORMAT_R32_TYPELESS;uv.Buffer.NumElements=i?12:40;uv.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
  dev->CreateUnorderedAccessView(i?indices.Get():out.Get(),nullptr,&uv,handle);handle.ptr+=step;}
 D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;hr(dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
 ComPtr<ID3D12CommandAllocator> allocator;hr(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
 ComPtr<ID3D12GraphicsCommandList> list;hr(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pso.Get(),IID_PPV_ARGS(&list)));
 list->SetComputeRootSignature(root.Get());ID3D12DescriptorHeap* heaps[]={heap.Get()};list->SetDescriptorHeaps(1,heaps);
 list->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());const unsigned control[4]={probe?4u:0u,probe?0u:1u,4u,0u};list->SetComputeRoot32BitConstants(1,4,control,0);list->Dispatch(1,1,1);
 D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=out.Get();b.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
 list->ResourceBarrier(1,&b);b.Transition.pResource=indices.Get();list->ResourceBarrier(1,&b);list->CopyBufferRegion(read.Get(),0,out.Get(),0,160);list->CopyBufferRegion(read.Get(),160,indices.Get(),0,48);hr(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
 ComPtr<ID3D12Fence> fence;hr(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));hr(queue->Signal(fence.Get(),1));
 HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);hr(fence->SetEventOnCompletion(1,event));auto waited=WaitForSingleObject(event,10000);CloseHandle(event);
 if(waited!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");hr(dev->GetDeviceRemovedReason());
 void* p;hr(read->Map(0,nullptr,&p));unsigned actual[52];std::memcpy(actual,p,sizeof(actual));read->Unmap(0,nullptr);
 int failures=0;
 if(probe){const unsigned expected[4]={1,1,0,0};for(unsigned i=0;i<4;++i){bool ok=actual[i]==expected[i];if(!ok)++failures;
  std::cout<<"probe["<<i<<"] expected="<<expected[i]<<" actual="<<actual[i]<<" "<<(ok?"PASS":"FAIL")<<"\n";}}
 else{
  for(unsigned i=0;i<4;++i){float v[5];std::memcpy(v,actual+(i+1)*5,20);auto expected=vertices[i+2];if(!posed){expected.position[0]=packed[i];expected.position[1]=packed[4+i];expected.position[2]=packed[8+i];expected.texture_coordinate[0]=uvInput[i*2];expected.texture_coordinate[1]=uvInput[i*2+1];}
   bool ok=v[0]==expected.position[0]&&v[1]==expected.position[1]&&v[2]==expected.position[2]&&v[3]==expected.texture_coordinate[0]+job.uv_shift[0]&&v[4]==expected.texture_coordinate[1]+job.uv_shift[1];if(!ok)++failures;
   std::cout<<(posed?"posed":"object-space")<<" repack["<<i<<"] position="<<v[0]<<","<<v[1]<<","<<v[2]<<" uv="<<v[3]<<","<<v[4]<<" "<<(ok?"PASS":"FAIL")<<"\n";
  }
  bool indicesOk=std::memcmp(actual+43,triangles,sizeof(triangles))==0;if(!indicesOk)++failures;
  std::cout<<"relative indices "<<(indicesOk?"PASS":"FAIL")<<"\n";
 }
 return failures;
}
int main(int argc,char** argv) try {
 if(argc<2)throw std::runtime_error("Expected shader directory [hardware]");
 const bool hardware=argc>2;std::cout<<(hardware?"hardware":"WARP")<<" D3D12, CPU vertex stride="<<sizeof(NativeWorldVertexGpu)<<" state stride="<<sizeof(NativeWorldStateGpu)<<"\n";
 int failures=run(std::string(argv[1])+"/repack.dxil",hardware,false);
 failures+=run(std::string(argv[1])+"/probe.dxil",hardware,true);
 failures+=run(std::string(argv[1])+"/repack.dxil",hardware,false,false);
 std::cout<<"RT layouts: 14 cases, "<<failures<<" failures\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 2;}
