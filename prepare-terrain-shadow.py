from pathlib import Path
import shutil,json,hashlib,re
r=Path('work/eirebuild-local');b=r/'evidence/pre-terrain-shadow';b.mkdir()
ns=['Game/Terrain.h','Game/Terrain.cpp','Game/WorldClient.cpp','Game/dx11/RendererDX11.h','Game/dx11/RendererDX11.cpp','Game/dx11/ModShadowDX11.h']
rows=[]
for n in ns:
 p=r/n;q=b/n;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q);rows.append(dict(file=n,sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
(b/'manifest.json').write_text(json.dumps(rows,indent=2))
ref=Path(r'C:/Users/samol/Documents/Codex/2026-09-13/new-chat/work/endurance-quiet/dynamic_lighting.cpp').read_text()
node=ref[ref.index('struct TerrainSelfShadowTopologyNode {'):ref.index('static TerrainSelfShadowTopologyField g_self_shadow_topology_active')]
a=ref.index('static bool TerrainSelfShadowTopologyHeightAt(',ref.index('static bool TerrainSelfShadowTopologyHeightAt(')+1);z=ref.index('static float TerrainSelfShadowNextSurfaceEvent',a);height=ref[a:z]
a=ref.index('static short TerrainClearanceQuantize(');z=ref.index('static bool TerrainSelfShadowBuildContinuousSurfaceField',a);clear=ref[a:z]
a=z;z=ref.index('\n#pragma pack(push, 1)',a);build=ref[a:z]
build=build.replace('static bool TerrainSelfShadowBuildContinuousSurfaceField(\n    unsigned char *terrain)', 'static bool Build(TerrainSelfShadowTopologyField *field, int add_x, int add_y, float rise_x, float rise_y)')
a=build.index('    TerrainSelfShadowTopologyField *field =');z=build.index('    const int abs_x',a)
build=build[:a]+'''    if (!field || !field->nodes || field->valid_nodes != field->node_count) return false;
'''+build[z:]
build=build.replace('g_config.terrain_self_shadow_length_scale > 1\n        ? (float)g_config.terrain_self_shadow_length_scale : 1.0f','3.0f')
out='''#pragma once
#include <windows.h>
#include <cmath>
#include <cfloat>
#include <cstring>
// Algorithm copied from dynamic_lighting.cpp e2c4867; typed input replaces executable offsets.
namespace TerrainShadowPort {
static const float kTerrainClearanceQuantization=1024.0f;
static const float kTerrainClearanceMaximumDistance=16.0f;
static const float kTerrainClearanceSoftRadius=1.0f;
'''+node+height+clear+build+'\n}\n'
(r/'Game/ModTerrainHorizon.h').write_text(out)
print('Extracted original horizon algorithm:',len(out.splitlines()),'lines')
