from pathlib import Path
r=Path('work/eirebuild-local/Game')
def edit(n,f):
 p=r/n;s=p.read_bytes().decode('cp1251').replace('\r\n','\n');p.write_bytes(f(s).replace('\n','\r\n').encode('cp1251'))
def rep(s,a,b):
 assert a in s,a;return s.replace(a,b,1)
edit('ModTerrainShadow.h',lambda s:s.replace('bool blending=previous && DWORD(now-published)<=1200;','bool blending=bool(previous);').replace('''float from=TerrainShadowPort::TerrainClearanceDecode(previous->field.surface_clearance[i]);distance=from+(distance-from)*amount;''','''float from=TerrainShadowPort::TerrainClearanceDecode(previous->field.surface_clearance[i]);
                float a=TerrainShadowPort::TerrainClearanceCoverage(from),b=TerrainShadowPort::TerrainClearanceCoverage(distance);
                pixels[i]=day && current->field.surface_valid[i]?static_cast<unsigned char>(a+(b-a)*amount+0.5f):0;
                continue;'''))
edit('Terrain.h',lambda s:s.replace('#pragma once','#pragma once\nclass ModTerrainShadow;').replace('    int nSegmentsToRelight;','    std::shared_ptr<ModTerrainShadow> modTerrainShadow;\n    int nSegmentsToRelight;').replace('    BOOL DrawBase(CRenderer *lpScene);','    BOOL DrawBase(CRenderer *lpScene);\n    void DrawModTerrainShadow(CRenderer *pRend);'))
edit('Terrain.cpp',lambda s:s.replace('#include "Terrain.h"','#include "Terrain.h"\n#include "ModTerrainShadow.h"').replace('    bIsLoaded = TRUE;','''    bIsLoaded = TRUE;
    if(!bCave) {
        const int width=nXSectors*N_CL_PSECTOR_S+1,height=nYSectors*N_CL_PSECTOR_S+1;
        if(static_cast<size_t>(width)*height<=2000000) {
            std::vector<float> heights(static_cast<size_t>(width)*height);
            for(int sy=0;sy<nYSectors;sy++)for(int sx=0;sx<nXSectors;sx++) {
                SSectVert2 *vertices=GetSectorGeometryData(sx,sy);
                for(int y=0;y<N_V_PSECTOR_S;y++)for(int x=0;x<N_V_PSECTOR_S;x++)
                    heights[(sy*N_CL_PSECTOR_S+y)*width+sx*N_CL_PSECTOR_S+x]=GetFloatValueByRange(vertices[y*N_V_PSECTOR_S+x].usZ,GetMaxZ());
            }
            modTerrainShadow=std::make_shared<ModTerrainShadow>();
            modTerrainShadow->Initialize(width,height,std::move(heights));
        }
    }
''',1).replace('void CTerrain::Free()\n{','void CTerrain::Free()\n{\n    modTerrainShadow.reset();').replace('    pSunLight = pLight;','''    pSunLight = pLight;
    if(modTerrainShadow)modTerrainShadow->Request(nAddCellsX,nAddCellsY,fpCA,fpCB,dvDir.z<0);
''',1)+'''
void CTerrain::DrawModTerrainShadow(CRenderer *pRend) {
    if(!modTerrainShadow)return;
    if(modTerrainShadow->Tick(GetTickCount()))
        pRend->GetD3D()->UpdateTerrainShadow(modTerrainShadow->width,modTerrainShadow->height,modTerrainShadow->pixels.data());
    if(!modTerrainShadow->ready)return;
    pRend->FlushBuffers();
    pRend->GetD3D()->SetTerrainShadowPass(true);
    DrawBase(pRend);
    pRend->FlushBuffers();
    pRend->GetD3D()->SetTerrainShadowPass(false);
}
''')
edit('dx11/RendererDX11.h',lambda s:s.replace('    void BeginModShadows();','    void UpdateTerrainShadow(unsigned width,unsigned height,const unsigned char *pixels);\n    void SetTerrainShadowPass(bool enabled);\n    void BeginModShadows();'))
edit('dx11/ModShadowDX11.h',lambda s:s.replace('    Ptr<ID3D11Texture2D> mask;','    Ptr<ID3D11Texture2D> terrainTexture;\n    Ptr<ID3D11ShaderResourceView> terrainSRV;\n    unsigned terrainWidth=0,terrainHeight=0;\n    Ptr<ID3D11Texture2D> mask;').replace('coverage, composite, alpha;','coverage, terrainCoverage, composite, alpha;').replace('    bool active=false;','    bool active=false,terrainPass=false;\n    unsigned TerrainWidth()const{return terrainWidth;}\n    unsigned TerrainHeight()const{return terrainHeight;}').replace('float4 shadow(P i):SV_TARGET {','float4 terrainShadow(P i):SV_TARGET {float value=opacity(source.Sample(samp,i.uv).r)*saturate(i.fogFactor);return value.xxxx;}\nfloat4 shadow(P i):SV_TARGET {').replace('        ps=Compile(shader,"copy","ps_4_0");','        ps=Compile(shader,"terrainShadow","ps_4_0");Check(d->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,terrainCoverage.GetAddressOf()));\n        ps=Compile(shader,"copy","ps_4_0");').replace('c->PSSetShader(coverage.Get(),nullptr,0);','c->PSSetShader(terrainPass?terrainCoverage.Get():coverage.Get(),nullptr,0);')+'''
''')
# Append public field upload methods inside existing class.
edit('dx11/ModShadowDX11.h',lambda s:s[:-3]+'''
    void UpdateTerrain(unsigned w,unsigned h,const unsigned char *pixels) {
        if(w!=terrainWidth||h!=terrainHeight){terrainSRV.Reset();terrainTexture.Reset();D3D11_TEXTURE2D_DESC t={};t.Width=w;t.Height=h;t.MipLevels=t.ArraySize=t.SampleDesc.Count=1;t.Format=DXGI_FORMAT_R8_UNORM;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            Check(d->CreateTexture2D(&t,nullptr,terrainTexture.GetAddressOf()));Check(d->CreateShaderResourceView(terrainTexture.Get(),nullptr,terrainSRV.GetAddressOf()));terrainWidth=w;terrainHeight=h;}
        c->UpdateSubresource(terrainTexture.Get(),0,nullptr,pixels,w,0);
    }
    void BindTerrainResource(){c->PSSetShaderResources(0,1,terrainSRV.GetAddressOf());}
};
''')
edit('dx11/RendererDX11.cpp',lambda s:rep(s,'''    std::shared_ptr<VertexBuffer<STLVertex1>> vertexBuf = m_caches->CreateOrUpdate(
        static_cast<STLVertex1 *>(pVertices), dwVertexCount);''','''    std::vector<STLVertex1> terrainVertices;
    if(modShadows && modShadows->terrainPass) {
        STLVertex1 *input=static_cast<STLVertex1 *>(pVertices);
        terrainVertices.assign(input,input+dwVertexCount);
        for(auto &v:terrainVertices){v.tu=(v.sx*2+0.5f)/modShadows->TerrainWidth();v.tv=(v.sy*2+0.5f)/modShadows->TerrainHeight();}
        pVertices=terrainVertices.data();
    }
    std::shared_ptr<VertexBuffer<STLVertex1>> vertexBuf = m_caches->CreateOrUpdate(
        static_cast<STLVertex1 *>(pVertices), dwVertexCount);''').replace('    this->deviceContext->DrawIndexed(dwIndexCount, 0, 0);','    if(modShadows && modShadows->terrainPass)modShadows->BindTerrainResource();\n    this->deviceContext->DrawIndexed(dwIndexCount, 0, 0);')+'''
void CDX11::UpdateTerrainShadow(unsigned w,unsigned h,const unsigned char *pixels){
    if(!modShadows)modShadows=std::make_shared<ModShadowDX11>(device.Get(),deviceContext.Get());
    modShadows->UpdateTerrain(w,h,pixels);
}
void CDX11::SetTerrainShadowPass(bool enabled){
    modShadows->terrainPass=enabled;modShadows->Bind();
    if(!enabled)deviceContext->PSSetShaderResources(0,1,myTexture.GetAddressOf());
}
''')
edit('WorldClient.cpp',lambda s:s.replace('        auto lbObjDrawShadow = [pRend]', '        lpTerrain->DrawModTerrainShadow(pRend);\n        auto lbObjDrawShadow = [pRend]',1))
