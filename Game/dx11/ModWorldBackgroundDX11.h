#pragma once
#include "../ModWorldBackground/Pipeline.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <limits>
#pragma comment(lib,"d3dcompiler.lib")

// Renderer-owned COM resources; only the scene/render thread calls this class.
// CPU snapshots have no device pointers. Pending uploads never replace the
// coherent visible snapshot until every buffer and texture is ready.
class ModWorldBackgroundDX11 {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    ID3D11Device* d;ID3D11DeviceContext* c;
    Ptr<ID3D11VertexShader> vs;Ptr<ID3D11PixelShader> ps;Ptr<ID3D11InputLayout> layout;
    Ptr<ID3D11Buffer> constants;Ptr<ID3D11SamplerState> sampler;
    Ptr<ID3D11RasterizerState> raster;Ptr<ID3D11BlendState> opaque,alpha,additive;
    Ptr<ID3D11DepthStencilState> depthWrite,depthRead;
    struct Geometry {Ptr<ID3D11Buffer> buffer;uint32_t count=0;std::array<float,3> center={};};
    struct Texture {Ptr<ID3D11Texture2D> image;Ptr<ID3D11ShaderResourceView> view;uint32_t width=0,height=0;};
    struct Entry {std::array<uint32_t,4> key;uint64_t geometryRevision=0,textureRevision=0;ET_Material2 material={};float x=0,y=0;
        std::shared_ptr<Geometry> geometry;std::shared_ptr<Texture> texture;};
    using Publication=ModWorldBackground::GroupPublication;
    struct Lane {std::vector<Entry> visible,pending;ModWorldBackground::GroupOffer offer;Publication::Set transaction;
        size_t cursor=0;uint64_t vertexOffset=0;uint32_t textureRow=0;Publication::Ticket ticket;};
    std::array<Lane,2> lanes;std::shared_ptr<Publication> publication;
    uint32_t epoch=0;uint64_t opaqueSerial=0;
    struct GroupError:std::runtime_error {HRESULT hr;GroupError(HRESULT value,const char* why):std::runtime_error(why),hr(value){}};
    struct Constants {float matrix[16],placement[4],fogRange[4],fogColor[4],depthRow[4];};
    struct State {
        Ptr<ID3D11VertexShader> vs;Ptr<ID3D11PixelShader> ps;Ptr<ID3D11GeometryShader> gs;
        Ptr<ID3D11HullShader> hs;Ptr<ID3D11DomainShader> ds;
        Ptr<ID3D11InputLayout> layout;Ptr<ID3D11Buffer> vertex,vc,pc;
        Ptr<ID3D11ShaderResourceView> texture;Ptr<ID3D11SamplerState> sampler;
        Ptr<ID3D11RasterizerState> raster;Ptr<ID3D11BlendState> blend;Ptr<ID3D11DepthStencilState> depth;
        UINT stride=0,offset=0,mask=0,stencil=0;FLOAT factor[4];D3D11_PRIMITIVE_TOPOLOGY topology;
        ID3D11DeviceContext* c;
        explicit State(ID3D11DeviceContext* context):c(context){
            c->VSGetShader(vs.GetAddressOf(),nullptr,nullptr);c->PSGetShader(ps.GetAddressOf(),nullptr,nullptr);
            c->GSGetShader(gs.GetAddressOf(),nullptr,nullptr);c->HSGetShader(hs.GetAddressOf(),nullptr,nullptr);c->DSGetShader(ds.GetAddressOf(),nullptr,nullptr);
            c->IAGetInputLayout(layout.GetAddressOf());c->IAGetPrimitiveTopology(&topology);c->IAGetVertexBuffers(0,1,vertex.GetAddressOf(),&stride,&offset);
            c->VSGetConstantBuffers(0,1,vc.GetAddressOf());c->PSGetConstantBuffers(0,1,pc.GetAddressOf());
            c->PSGetShaderResources(0,1,texture.GetAddressOf());c->PSGetSamplers(0,1,sampler.GetAddressOf());
            c->RSGetState(raster.GetAddressOf());c->OMGetBlendState(blend.GetAddressOf(),factor,&mask);c->OMGetDepthStencilState(depth.GetAddressOf(),&stencil);
        }
        ~State(){
            c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->GSSetShader(gs.Get(),nullptr,0);c->HSSetShader(hs.Get(),nullptr,0);c->DSSetShader(ds.Get(),nullptr,0);
            c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(topology);c->IASetVertexBuffers(0,1,vertex.GetAddressOf(),&stride,&offset);
            c->VSSetConstantBuffers(0,1,vc.GetAddressOf());c->PSSetConstantBuffers(0,1,pc.GetAddressOf());
            c->PSSetShaderResources(0,1,texture.GetAddressOf());c->PSSetSamplers(0,1,sampler.GetAddressOf());
            c->RSSetState(raster.Get());c->OMSetBlendState(blend.Get(),factor,mask);c->OMSetDepthStencilState(depth.Get(),stencil);
        }
    };
    static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("WorldBackground DX11 operation failed");}
    static Ptr<ID3DBlob> Compile(const char* source,const char* entry,const char* target){
        Ptr<ID3DBlob> result,error;HRESULT hr=D3DCompile(source,strlen(source),"ModWorldBackgroundDX11",nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,result.GetAddressOf(),error.GetAddressOf());
        if(FAILED(hr))throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"WorldBackground shader compile failed");return result;
    }
    static void GroupCheck(HRESULT hr,const char* operation){if(FAILED(hr))throw GroupError(hr,operation);}
    void Abandon(unsigned g,bool failure,bool cancel=true){auto& l=lanes[g];l.pending.clear();l.offer={};l.transaction={};l.ticket={};
        if(cancel)publication->Cancel(g,Publication::Now(),failure);}
    bool Begin(unsigned g,const ModWorldBackground::GroupOffer& offer,const std::array<float,16>& clip){
        auto state=publication->Read(g);if(state.phase!=Publication::Prepared)return false;
        if(!publication->Safe(state.pending,clip)){Abandon(g,false);return false;}
        const bool needsRoot=std::any_of(state.pending.items.begin(),state.pending.items.end(),[](const Publication::Item& item){return item.mode!=Publication::Keep;});
        if(needsRoot&&(!offer.root||offer.ticket.epoch!=state.accepted.epoch||offer.ticket.revision!=state.accepted.revision||offer.ticket.generation!=state.accepted.generation))return false;
        auto& l=lanes[g];l.pending.clear();l.transaction=std::move(state.pending);l.ticket=state.accepted;l.offer=needsRoot?offer:ModWorldBackground::GroupOffer{};
        l.cursor=0;l.vertexOffset=0;l.textureRow=0;
        if(!publication->Upload(l.ticket)){Abandon(g,false);return false;}return true;
    }
    bool Advance(unsigned g,ModWorldBackground::UploadBudget& budget,const std::array<float,16>& clip){
        auto& l=lanes[g];GroupCheck(d->GetDeviceRemovedReason(),"Background device health before upload");
        while(l.cursor<l.transaction.items.size()){
            const auto& item=l.transaction.items[l.cursor];
            if(item.mode==Publication::Keep){
                if(item.old<0||size_t(item.old)>=l.visible.size())throw GroupError(E_INVALIDARG,"Background committed KEEP slot missing");
                l.pending.push_back(l.visible[size_t(item.old)]);++l.cursor;continue;
            }
            if(!l.offer.root||item.source<0||size_t(item.source)>=l.offer.root->render->meshes.size())throw GroupError(E_INVALIDARG,"Background CPU group borrow missing");
            const auto& mesh=l.offer.root->render->meshes[size_t(item.source)].mesh;
            if(l.pending.size()==l.cursor){
                if(!budget.Allocate())return false;
                if(!mesh.vertexCount||mesh.vertexCount>65535||mesh.vertexCount%3||!mesh.vertices||!mesh.argb||!mesh.textureWidth||!mesh.textureHeight||mesh.textureWidth>4096||mesh.textureHeight>4096)throw GroupError(E_INVALIDARG,"Invalid native background mesh");
                Entry entry;entry.key=item.meta.key;entry.geometryRevision=item.meta.geometry;entry.textureRevision=item.meta.texture;entry.material=item.meta.material;entry.x=item.meta.x;entry.y=item.meta.y;
                if(item.mode==Publication::Geometry){
                    if(item.old<0||size_t(item.old)>=l.visible.size())throw GroupError(E_INVALIDARG,"Background committed texture slot missing");
                    entry.texture=l.visible[size_t(item.old)].texture;
                }
                // All changed geometry is immutable until commit. Never borrow
                // from pending/failed uploads or update the visible COM buffer.
                entry.geometry=std::make_shared<Geometry>();entry.geometry->count=mesh.vertexCount;
                for(unsigned a=0;a<3;++a)entry.geometry->center[a]=float((item.meta.box.low[a]+item.meta.box.high[a])*.5-(a==0?entry.x:a==1?entry.y:0));
                D3D11_BUFFER_DESC desc={};desc.ByteWidth=mesh.vertexCount*sizeof(ET_Vertex);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
                GroupCheck(d->CreateBuffer(&desc,nullptr,entry.geometry->buffer.GetAddressOf()),"Background group CreateBuffer");
                l.vertexOffset=0;l.textureRow=entry.texture?mesh.textureHeight:0;
                if(!entry.texture){entry.texture=std::make_shared<Texture>();entry.texture->width=mesh.textureWidth;entry.texture->height=mesh.textureHeight;
                    D3D11_TEXTURE2D_DESC texture={};texture.Width=mesh.textureWidth;texture.Height=mesh.textureHeight;texture.MipLevels=texture.ArraySize=texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_B8G8R8A8_UNORM;texture.Usage=D3D11_USAGE_DEFAULT;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                    GroupCheck(d->CreateTexture2D(&texture,nullptr,entry.texture->image.GetAddressOf()),"Background group CreateTexture2D");
                    GroupCheck(d->CreateShaderResourceView(entry.texture->image.Get(),nullptr,entry.texture->view.GetAddressOf()),"Background group CreateShaderResourceView");
                }
                l.pending.push_back(std::move(entry));
            }
            auto& entry=l.pending[l.cursor];const uint64_t total=uint64_t(mesh.vertexCount)*sizeof(ET_Vertex);
            if(l.vertexOffset<total){const auto n=budget.Take(total-l.vertexOffset,sizeof(ET_Vertex));if(!n)return false;
                D3D11_BOX box={UINT(l.vertexOffset),0,0,UINT(l.vertexOffset+n),1,1};c->UpdateSubresource(entry.geometry->buffer.Get(),0,&box,reinterpret_cast<const uint8_t*>(mesh.vertices)+l.vertexOffset,0,0);l.vertexOffset+=n;if(l.vertexOffset<total)return false;
            }
            if(l.textureRow<mesh.textureHeight){const auto pitch=uint64_t(mesh.textureWidth)*4;const auto n=budget.Take(uint64_t(mesh.textureHeight-l.textureRow)*pitch,pitch);if(!n)return false;
                const auto rows=uint32_t(n/pitch);D3D11_BOX box={0,l.textureRow,0,mesh.textureWidth,l.textureRow+rows,1};c->UpdateSubresource(entry.texture->image.Get(),0,&box,mesh.argb+size_t(l.textureRow)*mesh.textureWidth,UINT(pitch),0);l.textureRow+=rows;if(l.textureRow<mesh.textureHeight)return false;
            }
            ++l.cursor;
        }
        GroupCheck(d->GetDeviceRemovedReason(),"Background device health before publication");
        if(!publication->Safe(l.transaction,clip)){Abandon(g,false);return false;}
        if(!publication->MarkReady(l.ticket)||!publication->Commit(l.ticket)){Abandon(g,false);return false;}
        l.visible.swap(l.pending);l.pending.clear();publication->Released(g);l.offer={};l.transaction={};l.ticket={};return true;
    }

public:
    static const char* Shader(){return R"(
cbuffer Scene:register(b0){row_major float4x4 worldToClip;float4 placement;float4 fogRange;float4 fogColor;float4 depthRow;};
Texture2D image:register(t0);SamplerState smp:register(s0);
struct In {float3 p:POSITION;uint color:COLOR;float2 uv:TEXCOORD;};
struct Out {float4 p:SV_POSITION;float4 color:COLOR;float2 uv:TEXCOORD;float fog:FOG;};
Out vs(In v){Out o;float4 p=float4(v.p+float3(placement.xy,0),1);o.p=mul(worldToClip,p);o.uv=v.uv;
    o.color=float4((v.color>>16)&255,(v.color>>8)&255,v.color&255,v.color>>24)/255.0;
    float z=dot(depthRow,p);o.fog=fogRange.z!=0?floor(saturate((fogRange.y-z)/(fogRange.y-fogRange.x))*255)/255:1;return o;}
float4 ps(Out v):SV_TARGET{float4 color=image.Sample(smp,v.uv)*v.color;
    if(placement.z!=0)clip(color.a-placement.w-0.00001);color.rgb=lerp(fogColor.rgb,color.rgb,v.fog);return color;}
)";}
    static void CheckShaders(){Compile(Shader(),"vs","vs_4_0");Compile(Shader(),"ps","ps_4_0");}
    ModWorldBackgroundDX11(ID3D11Device* device,ID3D11DeviceContext* context):d(device),c(context){
        if(!d||!c)throw std::runtime_error("Background common device/context absent");
        const char* shader=Shader();
        auto code=Compile(shader,"vs","vs_4_0");Check(d->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,vs.GetAddressOf()));
        D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32_UINT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,20,D3D11_INPUT_PER_VERTEX_DATA,0}};
        Check(d->CreateInputLayout(elements,3,code->GetBufferPointer(),code->GetBufferSize(),layout.GetAddressOf()));code=Compile(shader,"ps","ps_4_0");Check(d->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,ps.GetAddressOf()));
        D3D11_BUFFER_DESC cb={};cb.ByteWidth=sizeof(Constants);cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;Check(d->CreateBuffer(&cb,nullptr,constants.GetAddressOf()));
        D3D11_SAMPLER_DESC sm={};sm.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sm.AddressU=sm.AddressV=sm.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sm.MaxLOD=D3D11_FLOAT32_MAX;Check(d->CreateSamplerState(&sm,sampler.GetAddressOf()));
        D3D11_RASTERIZER_DESC rs={};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;rs.MultisampleEnable=TRUE;Check(d->CreateRasterizerState(&rs,raster.GetAddressOf()));
        D3D11_BLEND_DESC b={};b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;Check(d->CreateBlendState(&b,opaque.GetAddressOf()));
        auto& rt=b.RenderTarget[0];rt.BlendEnable=TRUE;rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOp=D3D11_BLEND_OP_ADD;rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlendAlpha=D3D11_BLEND_ZERO;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;Check(d->CreateBlendState(&b,alpha.GetAddressOf()));rt.DestBlend=D3D11_BLEND_ONE;Check(d->CreateBlendState(&b,additive.GetAddressOf()));
        D3D11_DEPTH_STENCIL_DESC dep={};dep.DepthEnable=TRUE;dep.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;dep.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;Check(d->CreateDepthStencilState(&dep,depthWrite.GetAddressOf()));dep.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;Check(d->CreateDepthStencilState(&dep,depthRead.GetAddressOf()));
    }
    void Frame(const std::shared_ptr<ModWorldBackground::FrameSnapshot>& value,const WorldBackground::VisualEnvironment& environment,const std::array<float,4>& depthRow,DWORD fogColor,bool transparentPass){
        if(!value->publication||!(environment.valid&WorldBackground::VisualView))return;
        if(publication!=value->publication||epoch!=environment.epoch){lanes={};publication=value->publication;epoch=environment.epoch;opaqueSerial=0;}
        std::array<float,16> clip;std::copy(environment.view.begin(),environment.view.begin()+16,clip.begin());
        if(!transparentPass){
            const auto now=Publication::Now();publication->Pressure(clip,now);
            for(unsigned g=0;g<2;++g){const auto state=publication->Read(g);
                if(state.cancel||(lanes[g].ticket.revision&&!publication->Valid(lanes[g].ticket)))Abandon(g,false);
                if(!lanes[g].ticket.revision)try{Begin(g,value->offers[g],clip);}catch(const std::bad_alloc&){const auto health=d->GetDeviceRemovedReason();if(Publication::GlobalFailure(E_OUTOFMEMORY,health))throw;Abandon(g,true,false);publication->Failure(g,E_OUTOFMEMORY,health,now);}
            }
            ModWorldBackground::UploadBudget budget;
            for(unsigned g:publication->UploadOrder(now,++opaqueSerial))if(lanes[g].ticket.revision)try{Advance(g,budget,clip);}
                catch(const GroupError& error){const auto health=d->GetDeviceRemovedReason();char message[320];sprintf_s(message,"Background group=%u generation=%llu hr=%08lx health=%08lx operation=%s\n",g,lanes[g].ticket.generation,static_cast<unsigned long>(error.hr),static_cast<unsigned long>(health),error.what());OutputDebugStringA(message);
                    if(Publication::GlobalFailure(error.hr,health))throw;Abandon(g,true,false);publication->Failure(g,error.hr,health,Publication::Now());}
                catch(const std::bad_alloc&){const auto health=d->GetDeviceRemovedReason();if(Publication::GlobalFailure(E_OUTOFMEMORY,health))throw;Abandon(g,true,false);publication->Failure(g,E_OUTOFMEMORY,health,Publication::Now());}
        }
        std::vector<const Entry*> visible;for(const auto& lane:lanes)for(const auto& entry:lane.visible)visible.push_back(&entry);
        if(visible.empty())return;
        State restore(c);c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->GSSetShader(nullptr,nullptr,0);c->HSSetShader(nullptr,nullptr,0);c->DSSetShader(nullptr,nullptr,0);
        c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->RSSetState(raster.Get());
        c->VSSetConstantBuffers(0,1,constants.GetAddressOf());c->PSSetConstantBuffers(0,1,constants.GetAddressOf());c->PSSetSamplers(0,1,sampler.GetAddressOf());
        std::vector<size_t> order;for(size_t i=0;i<visible.size();++i)order.push_back(i);
        auto depth=[&](const Entry& e){return depthRow[0]*(e.geometry->center[0]+e.x)+depthRow[1]*(e.geometry->center[1]+e.y)+depthRow[2]*e.geometry->center[2]+depthRow[3];};
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){const bool aa=visible[a]->material.pass==ET_ALPHA,ba=visible[b]->material.pass==ET_ALPHA;if(aa!=ba)return !aa;return aa&&depth(*visible[a])>depth(*visible[b]);});
        for(size_t index:order){const auto& e=*visible[index];if((e.material.pass==ET_ALPHA)!=transparentPass)continue;Constants cb={};std::copy(environment.view.begin(),environment.view.begin()+16,cb.matrix);
            cb.placement[0]=e.x;cb.placement[1]=e.y;cb.placement[2]=(e.material.flags&ET_MATERIAL_ALPHA_TEST)?1.0f:0.0f;cb.placement[3]=e.material.alphaRef/255.0f;
            cb.fogRange[0]=environment.atmosphereRange[0];cb.fogRange[1]=environment.atmosphereRange[1];cb.fogRange[2]=(environment.valid&WorldBackground::VisualAtmosphere)&&cb.fogRange[1]>cb.fogRange[0]?1.0f:0.0f;
            cb.fogColor[0]=((fogColor>>16)&255)/255.0f;cb.fogColor[1]=((fogColor>>8)&255)/255.0f;cb.fogColor[2]=(fogColor&255)/255.0f;
            std::copy(depthRow.begin(),depthRow.end(),cb.depthRow);
            c->UpdateSubresource(constants.Get(),0,nullptr,&cb,0,0);UINT stride=sizeof(ET_Vertex),offset=0;c->IASetVertexBuffers(0,1,e.geometry->buffer.GetAddressOf(),&stride,&offset);
            c->PSSetShaderResources(0,1,e.texture->view.GetAddressOf());const bool transparent=e.material.pass==ET_ALPHA;
            c->OMSetBlendState((e.material.flags&ET_MATERIAL_ADDITIVE)?additive.Get():transparent?alpha.Get():opaque.Get(),nullptr,~0u);c->OMSetDepthStencilState(transparent?depthRead.Get():depthWrite.Get(),0);c->Draw(e.geometry->count,0);
        }
    }
};
