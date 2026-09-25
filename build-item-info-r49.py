import os,json,pathlib,subprocess,sys,time
r=pathlib.Path(__file__).resolve().parent
stage=r/'evidence/addon-integration/stage2-r49'
p=json.loads((r/'evidence/addon-integration/r28-build-properties.json').read_text())
env={k.upper():v for k,v in os.environ.items()};env['INCLUDE']=p['IncludePath'];env['LIB']=p['LibraryPath']
tools=pathlib.Path(p['VCToolsInstallDir'])/'bin/HostX86/x86'
env['PATH']=str(tools)+r';C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86;'+env.get('PATH','')
results=[]
with (stage/'build.log').open('wb') as log:
 for tool,rsp in [('cl.exe','pch.rsp'),('cl.exe','compile.rsp'),('link.exe','link.rsp')]:
  begin=time.time(); proc=subprocess.run([str(tools/tool),'@'+str(stage/rsp)],cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
  results.append({'tool':tool,'exit':proc.returncode,'seconds':time.time()-begin})
  (stage/'build-result.json').write_text(json.dumps({'steps':results,'complete':False},indent=2))
  if proc.returncode:sys.exit(proc.returncode)
(stage/'build-result.json').write_text(json.dumps({'steps':results,'complete':True},indent=2))
print('Stage 2 Release Win32 compiler and linker exit 0')
