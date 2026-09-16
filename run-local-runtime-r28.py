import os,runpy,sys
paths={'USERPROFILE':r'C:\Users\samol','SystemRoot':r'C:\WINDOWS','WINDIR':r'C:\WINDOWS','TEMP':r'C:\Users\samol\AppData\Local\Temp','TMP':r'C:\Users\samol\AppData\Local\Temp','ProgramData':r'C:\ProgramData','ALLUSERSPROFILE':r'C:\ProgramData','LOCALAPPDATA':r'C:\Users\samol\AppData\Local','APPDATA':r'C:\Users\samol\AppData\Roaming'}
for k,v in paths.items():
 if not os.path.isdir(v):raise RuntimeError('Missing build environment directory '+k)
 os.environ[k]=v
script=sys.argv.pop(1)
runpy.run_path(script,run_name='__main__')
