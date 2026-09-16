import os,json,pathlib,subprocess,sys
r=pathlib.Path(__file__).resolve().parent
p=json.loads((r/'evidence/addon-integration/r28-build-properties.json').read_text())
env={k.upper():v for k,v in os.environ.items()};env['INCLUDE']=p['IncludePath'];env['LIB']=p['LibraryPath']
for key in ['TEMP','TMP','SYSTEMROOT','WINDIR','USERPROFILE']:
 if key in env:env[key]=env[key].replace('\\\\','\\')
tools=pathlib.Path(p['VCToolsInstallDir'])/'bin/HostX86/x86'
env['PATH']=str(tools)+';'+r'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86'+';'+env.get('PATH','')
with (r/'evidence/build-87-unit-audio-direct.log').open('wb') as log:
 for tool,rsp in ([('link.exe','r29-link.rsp')] if '--link-only' in sys.argv else [('cl.exe','r29-cl.rsp'),('link.exe','r29-link.rsp')]):
  result=subprocess.run([str(tools/tool),'@'+str(r/'evidence/addon-integration'/rsp)],cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
  if result.returncode:print(tool,'failed',result.returncode);sys.exit(result.returncode)
print('Direct source compile + exact Release x86 link PASS')
