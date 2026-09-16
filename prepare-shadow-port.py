from pathlib import Path
import shutil,hashlib,json
r=Path('work/eirebuild-local'); b=r/'evidence/pre-shadows'; b.mkdir(exist_ok=True)
names=['Game/Object3DClientSpecific.cpp','Game/GeometryContainer.cpp','Game/Texture.h','Game/Texture.cpp','Game/WorldClient.cpp','Game/dx11/RendererDX11.cpp','Game/dx11/RendererDX11.h']
rows=[]
for n in names:
 p=r/n; dst=b/n;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst);rows.append({'file':n,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(b/'manifest.json').write_text(json.dumps(rows,indent=2))
ref=Path(r'C:/Users/samol/Documents/Codex/2026-09-13/new-chat/work/endurance-quiet/shadow_fixes.cpp').read_text()
a=ref.index('struct TreeShadowRasterVertex {');z=ref.index('static void InitializeTreeShadowCoverage()',a)
funca=ref.index('static bool RasterTreeShadowAlpha(');funcz=ref.index('\nextern "C" int __cdecl DispatchTreeShadowAlpha',funca)
s='#pragma once\n#include <windows.h>\n#include <cstdint>\n// Adapted from quiet e2c4867 shadow_fixes.cpp. Address hooks and telemetry excluded.\nnamespace ModShadow {\n'
s+=ref[a:z]
s+='''static const DWORD g_tree_shadow_alpha_density_percent = 75;
static const DWORD g_tree_shadow_alpha_dither_mode = 1;
static const bool g_tree_shadow_fast_raster = true;
static const bool g_tree_shadow_prebake_density = false;
static unsigned char g_tree_shadow_coverage[4096];
inline void InitCoverage() { static bool ready=false; if(ready)return; for(int y=0;y<64;++y)for(int x=0;x<64;++x)g_tree_shadow_coverage[y*64+x]=static_cast<unsigned char>(TreeShadowCoverageHash(x,y));ready=true; }
'''
s+=ref[funca:funcz].replace('    if (!triangle ||','    InitCoverage();\n    if (!triangle ||',1)
s+='\n}\n';(r/'Game/ModShadowRaster.h').write_text(s)
