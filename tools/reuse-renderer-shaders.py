"""Package verified original shader resources without compiling any HLSL."""
import argparse,ctypes as c,hashlib,json,re,subprocess,sys
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',required=True);p.add_argument('--executable',type=Path,required=True)
    p.add_argument('--sha256',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--rc',type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parent.parent
    if hashlib.sha256(a.executable.read_bytes()).hexdigest()!=a.sha256.lower():raise ValueError('Shader donor SHA mismatch')
    if a.output.exists():raise ValueError('New output directory required')
    a.output.mkdir(parents=True);out=a.output.resolve();proof=out/'sources.json'
    subprocess.run([sys.executable,str(root/'tools/check-environment-renderer.py'),'--baseline',a.baseline,'--report',str(proof)],check=True)
    kernel=c.WinDLL('kernel32',use_last_error=True)
    kernel.LoadLibraryExW.argtypes=[c.c_wchar_p,c.c_void_p,c.c_uint32];kernel.LoadLibraryExW.restype=c.c_void_p
    kernel.FindResourceW.argtypes=[c.c_void_p,c.c_wchar_p,c.c_void_p];kernel.FindResourceW.restype=c.c_void_p
    kernel.SizeofResource.argtypes=[c.c_void_p,c.c_void_p];kernel.SizeofResource.restype=c.c_uint32
    kernel.LoadResource.argtypes=[c.c_void_p,c.c_void_p];kernel.LoadResource.restype=c.c_void_p
    kernel.LockResource.argtypes=[c.c_void_p];kernel.LockResource.restype=c.c_void_p
    callback=c.WINFUNCTYPE(c.c_int,c.c_void_p,c.c_void_p,c.c_void_p,c.c_ssize_t)
    kernel.EnumResourceNamesW.argtypes=[c.c_void_p,c.c_void_p,callback,c.c_ssize_t];kernel.EnumResourceNamesW.restype=c.c_int
    kernel.FreeLibrary.argtypes=[c.c_void_p]
    groups={name:[] for name in ['eid3d11_shaders','eid3d11_dxil','eid3d11_d3d12_shaders','eid3d11_camp_art']}
    errors=[];counts={name:0 for name in groups}
    module=kernel.LoadLibraryExW(str(a.executable.resolve()),None,2)
    if not module:raise c.WinError(c.get_last_error())
    def visit(handle,kind,address,context):
        try:
            if address<=65535:return 1
            name=c.wstring_at(address)
            if name=='EID3D11_SHADERS':group='eid3d11_shaders'
            elif re.fullmatch('X[0-9A-F]{32}',name):group='eid3d11_dxil'
            elif name.startswith('D3D12_'):group='eid3d11_d3d12_shaders'
            elif name.startswith('EID3D11_'):group='eid3d11_camp_art'
            else:return 1
            resource=kernel.FindResourceW(handle,name,c.c_void_p(10));size=kernel.SizeofResource(handle,resource)
            data=kernel.LockResource(kernel.LoadResource(handle,resource))
            if not data or not size:raise c.WinError(c.get_last_error())
            filename=name+'.bin';(out/filename).write_bytes(c.string_at(data,size))
            groups[group].append(name+' RCDATA "'+filename+'"\n');counts[group]+=1
            if name=='EID3D11_SHADERS':(out/'eid3d11_shaders.bin').write_bytes(c.string_at(data,size))
            return 1
        except BaseException as e:errors.append(str(e));return 0
    cb=callback(visit)
    try:
        if not kernel.EnumResourceNamesW(module,c.c_void_p(10),cb,0) or errors:raise RuntimeError(errors or c.WinError(c.get_last_error()))
    finally:kernel.FreeLibrary(module)
    if counts['eid3d11_shaders']!=1 or counts['eid3d11_dxil']<100 or any(not n for n in counts.values()):raise ValueError('Incomplete shader donor')
    files=[]
    for group,rows in groups.items():
        rc=out/(group+'.rc');rc.write_text(''.join(rows),encoding='ascii')
        subprocess.run([str(a.rc.resolve()),'/nologo','/fo',group+'.res',rc.name],cwd=out,check=True)
        file=out/(group+'.res');files.append({'path':file.name,'sha256':hashlib.sha256(file.read_bytes()).hexdigest()})
    file=out/'eid3d11_shaders.bin';files.append({'path':file.name,'sha256':hashlib.sha256(file.read_bytes()).hexdigest()})
    sources=json.loads(proof.read_text())
    manifest={'format':'renderer-bytecode-reuse.v1','baseline':sources['baseline'],'donor':str(a.executable.resolve()),'donor_sha256':a.sha256.lower(),'counts':counts,'sources':sources['files'],'files':files}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({'counts':counts,'compiled_hlsl':0,'output':str(out)}))

if __name__=='__main__':main()
