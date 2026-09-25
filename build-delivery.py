"""Build stage 2 attack delivery using preserved Release Win32 options; retain exit status."""
import os,json,pathlib,subprocess,sys,time
r=pathlib.Path(__file__).resolve().parent
ev=r/'evidence/addon-integration'; stage=ev/'stage2-r33'
p=json.loads((ev/'r28-build-properties.json').read_text())
env={k.upper():v for k,v in os.environ.items()};env['INCLUDE']=p['IncludePath'];env['LIB']=p['LibraryPath']
tools=pathlib.Path(p['VCToolsInstallDir'])/'bin/HostX86/x86'
env['PATH']=str(tools)+';'+r'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86'+';'+env.get('PATH','')
results=json.loads((stage/'build-result.json').read_text())['steps'][:1] if '--link-only' in sys.argv else []
with (stage/'build.log').open('wb') as log:
 for tool,rsp in ([('link.exe','link.rsp')] if '--link-only' in sys.argv else [('cl.exe','compile-loops.rsp' if '--loops-only' in sys.argv else 'compile.rsp'),('link.exe','link.rsp')]):
  begin=time.time(); process=subprocess.run([str(tools/tool),'@'+str(stage/rsp)],cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
  results.append(dict(tool=tool,exit=process.returncode,seconds=time.time()-begin))
  (stage/'build-result.json').write_text(json.dumps(dict(steps=results,complete=False),indent=2))
  if process.returncode:sys.exit(process.returncode)
(stage/'build-result.json').write_text(json.dumps(dict(steps=results,complete=True),indent=2))
print('Stage 2 Release Win32 compiler and linker exit 0')
