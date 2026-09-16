from pathlib import Path
r=Path('work/eirebuild-local/Game')
def edit(n,f):
 p=r/n;s=p.read_bytes().decode('cp1251').replace('\r\n','\n');s=f(s);p.write_bytes(s.replace('\n','\r\n').encode('cp1251'))
def rep(s,a,b):
 assert a in s,a;return s.replace(a,b,1)
edit('dx11/RendererDX11.h',lambda s:rep(rep(s,'class CDXGISurface;','class ModShadowDX11;\nclass CDXGISurface;'),'    DWORD m_width, m_height;','    std::unique_ptr<ModShadowDX11> modShadows;\n    DWORD m_width, m_height;').replace('    void BeginFrame();','    void BeginModShadows();\n    void EndModShadows();\n    std::vector<WORD> ReadModShadowAlpha(ID3D11ShaderResourceView *source);\n    void BeginFrame();'))
edit('dx11/RendererDX11.cpp',lambda s:rep(rep(s,'#include "RendererDX11.h"','#include "RendererDX11.h"\n#include "ModShadowDX11.h"'),'    this->deviceContext->DrawIndexed(dwIndexCount, 0, 0);','    if (modShadows && modShadows->active) modShadows->Bind();\n    this->deviceContext->DrawIndexed(dwIndexCount, 0, 0);')+'''
void CDX11::BeginModShadows() {
    if(!modShadows) modShadows=std::make_unique<ModShadowDX11>(device.Get(),deviceContext.Get());
    modShadows->Begin(m_width,m_height);
}
void CDX11::EndModShadows() { if(modShadows)modShadows->End(); }
std::vector<WORD> CDX11::ReadModShadowAlpha(ID3D11ShaderResourceView *source) {
    if(!modShadows)modShadows=std::make_unique<ModShadowDX11>(device.Get(),deviceContext.Get());
    return modShadows->ReadAlpha(source);
}
''')
edit('Texture.h',lambda s:rep(s,'    CBYTEArray userdata;','    CBYTEArray userdata;\n    std::vector<WORD> modShadowAlpha;').replace('    CDXGISurface *GetSurface() const','    WORD *GetModShadowAlpha();\n    CDXGISurface *GetSurface() const'))
edit('Texture.cpp',lambda s:rep(s,'#include "StdAfx.h"','#include "StdAfx.h"\n#include "dx11/RendererDX11.h"')+'''
WORD *CTexture::GetModShadowAlpha() {
    if(!pSurface)return nullptr;
    if(modShadowAlpha.empty()) modShadowAlpha=theRenderer.GetD3D()->ReadModShadowAlpha(pSurface->GetShaderResource().Get());
    return modShadowAlpha.data();
}
''')
# Invalidate normalized alpha with original texture lifecycle (no pointer-key cache).
edit('Texture.cpp',lambda s:s.replace('void CTexture::Unload()\n{','void CTexture::Unload()\n{\n    modShadowAlpha.clear();').replace('void CTexture::Destroy()\n{','void CTexture::Destroy()\n{\n    modShadowAlpha.clear();'))
edit('GeometryContainer.cpp',lambda s:rep(rep(s,'#include "StdAfx.h"','#include "StdAfx.h"\n#include "ModShadowRaster.h"'),'''        /*
        if ( rp.lpSrcTexture )
                pGrPU->RasterizeTriangleWithAlpha( rp );
        else
        */
        pGrPU->RasterizeTriangle(rp);''','''        if (rp.lpSrcTexture) {
            ModShadow::TreeShadowRasterTriangle triangle = {
                {{rp.x1,rp.y1,rp.tu1,rp.tv1},{rp.x2,rp.y2,rp.tu2,rp.tv2},{rp.x3,rp.y3,rp.tu3,rp.tv3}},
                rp.lpSrcTexture,rp.lpDstTexture,LONG(rp.dwSrcPitch),LONG(rp.dwDstPitch)};
            ModShadow::RasterTreeShadowAlpha(&triangle);
        } else pGrPU->RasterizeTriangle(rp);'''))
edit('Object3DClientSpecific.cpp',lambda s:rep(s,'''        // TODO - broken at high poly
        shadowbuffer.SetSize(1024 * 4); // ARRAY BOUNDS CHECK
        shadowresult.SetSize(1024 * 4); // ARRAY BOUNDS CHECK''','''        // Geometry counts are groups of four, as returned by ProjectShadow.
        DWORD shadowCapacity = 0;
        for(int k=0;k<lpObject->figures.GetSize();++k) {
            const SXYZW4 *plots; const SVertexComponents *components; const WORD *indices;
            DWORD count=0,indexCount=0;
            if(lpObject->pGeometry && lpObject->pGeometry->GetRangeGeometryData(lpObject->figures[k]->GetName(),plots,components,indices,count,indexCount)) shadowCapacity += count*4;
        }
        if(!shadowCapacity)return TRUE;
        shadowbuffer.SetSize(shadowCapacity);
        shadowresult.SetSize(shadowCapacity);''').replace('''            if (lpObject->pGeometry)
                dwShadowBufferShift += lpObject->pGeometry->RenderShadow(''','''            CTexture *casterTexture = FindTexture(lpObject->figures[j]->GetTextureTypeID());
            renderparam.lpSrcTexture = lpObject->WhoAmI()==HPC_TREE && casterTexture
                ? casterTexture->GetModShadowAlpha() : nullptr;
            if (lpObject->pGeometry)
                dwShadowBufferShift += lpObject->pGeometry->RenderShadow(''',1))
# Pass boundaries before any projected shadow submission; flush both boundaries.
edit('WorldClient.cpp',lambda s:rep(rep(s,'    // draw shadows\n','    // draw shadows\n'),'    if (CheckFlag(WC_RENDER_SHADOWS))\n    {','    if (CheckFlag(WC_RENDER_SHADOWS))\n    {\n        pRend->FlushBuffers();\n        pRend->GetD3D()->BeginModShadows();').replace('''        pRend->FlushBuffers();
        pRend->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, TRUE);''','''        pRend->FlushBuffers();
        pRend->GetD3D()->EndModShadows();
        pRend->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, TRUE);''',1))
print('Object alpha, variable geometry buffers, DX11 max-opacity pass connected.')
