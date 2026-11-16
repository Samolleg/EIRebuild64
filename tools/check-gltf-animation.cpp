#include "gltf_animation.h"
#include "native_shader.h"
#include "gltf_playback.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
#include <functional>
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
static std::vector<NodeGpu> gpu(const Animation& a,const Playback& playback){
    auto packed=pack(a,"test");const unsigned base=2,total=base+packed->nodes+packed->morphs;
    std::vector<unsigned char> bytes(32+packed->bytes.size());
    unsigned header[8]{0,0,0,0,0,0,32,base};std::memcpy(bytes.data(),header,32);std::memcpy(bytes.data()+32,packed->bytes.data(),packed->bytes.size());
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
    PoseGpu input;input.clip=playback.clip;input.previous=playback.previous;input.time=playback.time;input.previousTime=playback.previous_time;input.blend=playback.blend;
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
int main()try{
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
        for(unsigned i=0;i<a.nodes.size();++i)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
            check_near(actual[i].rows[r][c],expected.joints[i][r*4+c],"CPU/GPU joint mismatch");
        for(unsigned i=0;i<a.morphs.size();++i)for(unsigned k=0;k<9;++k)
            check_near(actual[a.nodes.size()+i].rows[k/3][k%3],expected.morphs[i][k],"CPU/GPU morph mismatch");
        for(const auto& n:actual)check(n.generation[0]==1&&n.generation[1]==2,"generation stamp");
    }
    std::cout<<"PASS GPU readback: STEP/LINEAR/CUBICSPLINE, quaternion, hierarchy/scale, morphs, blend/defaults/endpoints\n";
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
    std::cout<<"PASS bindings, loop/clamp, event phases/deduplication/seek, transitions, cubic motion bounds\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
