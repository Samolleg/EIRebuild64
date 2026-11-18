"""Assert the environment integration does not alter existing shader producers."""
import argparse,hashlib,json,subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline',required=True);p.add_argument('--report',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parent.parent
def git(*args):return subprocess.check_output(['git','-C',str(root),*args])
baseline=git('rev-parse',a.baseline).decode().strip()
paths=git('ls-tree','-r','--name-only',baseline,'external/owned-renderer/Source','external/owned-renderer/Host','external/owned-renderer/Tools','OwnedRenderer').decode().splitlines()
rows=[];changed=[]
for path in paths:
    if not path.endswith(('.h','.cpp','.hlsl','.hlsli','.vcxproj','.props','.mak','.rc')):continue
    before=git('show',baseline+':'+path)
    protected=path.endswith(('native_shader.h','static_lod.h','engine_bridge.h','renderer.h','native_instance_flags.h','shader_embed.cpp','RendererShaders.vcxproj','Renderer.props','D3D12Sdk.props','D3D12Shaders.mak','camp_art.rc','.hlsl','.hlsli')) or any(token in before for token in (b'SV_Position',b'cbuffer ',b'register(t',b'RWStructuredBuffer<'))
    if not protected:continue
    after=(root/path).read_bytes()
    rows.append({'path':path,'sha256':hashlib.sha256(after).hexdigest(),'unchanged':after==before})
    if before!=after:changed.append(path)
result={'baseline':baseline,'protected_files':len(rows),'changed':changed,'files':rows}
a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'protected_files':len(rows),'changed':changed}));raise SystemExit(bool(changed))
