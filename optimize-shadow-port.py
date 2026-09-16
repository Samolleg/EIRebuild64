from pathlib import Path
r=Path('work/eirebuild-local/Game')
def edit(n,f):
 p=r/n;s=p.read_bytes().decode('cp1251').replace('\r\n','\n');p.write_bytes(f(s).replace('\n','\r\n').encode('cp1251'))
def rep(s,a,b):
 assert a in s,a;return s.replace(a,b,1)
edit('ModShadowRaster.h',lambda s:s.replace('g_tree_shadow_prebake_density = false','g_tree_shadow_prebake_density = true').replace('static bool RasterTreeShadowAlpha(','''// e2c4867 preparation: thin partial masks once; preserve opaque fast path.
inline bool PrepareMask(WORD *pixels, unsigned side) {
    InitCoverage();
    bool opaque=true;for(unsigned i=0;i<side*side;i++)if(!(pixels[i]&0x8000)){opaque=false;break;}
    if(opaque)return true;
    for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++)
        if(g_tree_shadow_coverage[((y&63)<<6)|(x&63)] >= (75u*256u+99u)/100u) pixels[y*side+x]=0;
    return false;
}
static bool RasterTreeShadowAlpha('''))
edit('Texture.h',lambda s:s.replace('    std::vector<WORD> modShadowAlpha;','    std::vector<WORD> modShadowAlpha;\n    bool modShadowOpaque = false;'))
edit('Texture.cpp',lambda s:s.replace('#include "Texture.h"','#include "Texture.h"\n#include "ModShadowRaster.h"').replace('    if(modShadowAlpha.empty()) modShadowAlpha=theRenderer.GetD3D()->ReadModShadowAlpha(pSurface->GetShaderResource().Get());\n    return modShadowAlpha.data();','    if(modShadowAlpha.empty()) {\n        modShadowAlpha=theRenderer.GetD3D()->ReadModShadowAlpha(pSurface->GetShaderResource().Get());\n        modShadowOpaque=ModShadow::PrepareMask(modShadowAlpha.data(),256);\n    }\n    return modShadowOpaque ? nullptr : modShadowAlpha.data();'))
edit('Object3DClientSpecific.h',lambda s:s.replace('    CTRect<float> shadowrect;', '    DWORD modShadowLastBuild = 0;\n    CTRect<float> shadowrect;'))
edit('Object3DClientSpecific.cpp',lambda s:rep(s,'    // TODO WORLD MATRIX TRANSLATION\n    lpObject->ResetFlag(OBJ_LIGHTED);','    // DX11 relighting is unconditional, but must not invalidate a cached static shadow.\n    const DWORD shadowLightingWasValid = lpObject->CheckFlag(OBJ_LIGHTED);\n    lpObject->ResetFlag(OBJ_LIGHTED);').replace('DWORD dwObjectWasLighted = lpObject->CheckFlag(OBJ_LIGHTED);','DWORD dwObjectWasLighted = shadowLightingWasValid;').replace('#include "TextureManager.h"','#include "TextureManager.h"\n#include "ModShadowSchedule.h"').replace('    if (IsNeedRecalcShadow())\n    {','''    const DWORD shadowNow = GetTickCount();
    bool rebuildShadow = IsNeedRecalcShadow();
    if (!rebuildShadow && shadowvertices.GetSize()) {
        DWORD width,height; pRend->GetD3D()->GetCurrentMode(width,height);
        const CRect &screen = lpObject->rcScreenRect;
        const unsigned long long area = static_cast<unsigned long long>(Max(0,screen.Width())) * Max(0,screen.Height());
        rebuildShadow = ModShadowSchedule::ForceRefresh(shadowNow,modShadowLastBuild,
            lpObject->CheckFlag(OBJ_VISIBLE)!=0,area*1000 >= static_cast<unsigned long long>(width)*height*10);
    }
    LARGE_INTEGER shadowStarted;
    if (rebuildShadow)
    {
        QueryPerformanceCounter(&shadowStarted);
        ++ModShadowSchedule::rebuilt;''',1).replace('''        // reset previous build shadow bounding volume
        if (shadowvertices.GetSize() != 0)
        {
            bvShadowBV.dwFlags = 0;
        }
        ResetFlag(OCS_RESHADOW);''','''        // Bound the projected mesh, not the caster: long shadows may remain visible offscreen.
        bvShadowBV.dwFlags = 0;
        if (shadowvertices.GetSize()) {
            CPlot3D lo(FLT_MAX,FLT_MAX,FLT_MAX),hi(-FLT_MAX,-FLT_MAX,-FLT_MAX);
            for(int v=0;v<shadowvertices.GetSize();v++)for(int lane=0;lane<4;lane++) {
                const SXYZW4 &p=shadowvertices[v];
                lo.x=Min(lo.x,p.x[lane]);lo.y=Min(lo.y,p.y[lane]);lo.z=Min(lo.z,p.z[lane]);
                hi.x=Max(hi.x,p.x[lane]);hi.y=Max(hi.y,p.y[lane]);hi.z=Max(hi.z,p.z[lane]);
            }
            CPlot3D center=(lo+hi)*.5f;
            bvShadowBV.ptCenter=center-lpObject->GetCP();
            bvShadowBV.SetRadius(fabs(hi-center)+1.0f);
        }
        modShadowLastBuild=shadowNow;
        LARGE_INTEGER finished;QueryPerformanceCounter(&finished);
        ModShadowSchedule::generationTicks += finished.QuadPart-shadowStarted.QuadPart;
        ResetFlag(OCS_RESHADOW);''').replace('''    if (shadowvertices.GetSize() == 0)
        return TRUE;''','''    if(!rebuildShadow)++ModShadowSchedule::reused;
    if (shadowvertices.GetSize() == 0)
        return TRUE;''',1).replace('    DWORD dwColor = RGBA_MAKE(255, 255, 255, 128);','''    SBoundingVolume projected = bvShadowBV;
    projected.ptCenter += lpObject->GetCP();
    if(projected.dwFlags && pGPU->CheckVisibility(projected)==TRV_INVISIBLE) {
        ++ModShadowSchedule::culled;return TRUE;
    }
    DWORD dwColor = RGBA_MAKE(255, 255, 255, 128);''',1).replace('void CObject3DClientSpecific::RemoveShadowInformation()\n{','void CObject3DClientSpecific::RemoveShadowInformation()\n{\n    modShadowLastBuild=0;'))
edit('WorldClient.cpp',lambda s:s.replace('#include "Renderer.h"','#include "Renderer.h"\n#include "ModShadowSchedule.h"').replace('        pRend->GetD3D()->BeginModShadows();','        ModShadowSchedule::BeginFrame();\n        pRend->GetD3D()->BeginModShadows();').replace('        pRend->GetD3D()->EndModShadows();','        pRend->GetD3D()->EndModShadows();\n        ModShadowSchedule::EndFrame();'))
# Bind common shadow state once, reapply only after actual legacy render-state changes.
edit('dx11/ModShadowDX11.h',lambda s:s.replace('saved.ds.Get());active=true;','saved.ds.Get());active=true;Bind();'))
edit('dx11/RendererDX11.cpp',lambda s:s.replace('    this->SetAlphaRef(alpha_test_enable ? alpharef : 0);','    this->SetAlphaRef(alpha_test_enable ? alpharef : 0);\n    if(modShadows && modShadows->active)modShadows->Bind();').replace('    this->deviceContext->PSSetShader(m_shaders->ps_main.GetShader(), NULL, 0);','    if(!modShadows || !modShadows->active)\n        this->deviceContext->PSSetShader(m_shaders->ps_main.GetShader(), NULL, 0);').replace('    if (modShadows && modShadows->active) modShadows->Bind();\n',''))
