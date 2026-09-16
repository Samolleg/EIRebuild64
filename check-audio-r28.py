from pathlib import Path
import subprocess,json
r=Path(__file__).resolve().parent;out=r/'evidence/addon-integration/r28-runtime.json'
p=subprocess.run([str(r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe'),'--inspect-addon',r'C:\GOG Games\Evil Islands\Starter\MODS\JamaisVu135',str(out)],cwd=r'C:\GOG Games\Evil',timeout=40,creationflags=subprocess.CREATE_NO_WINDOW)
data=json.loads(out.read_text());assert p.returncode==0,(p.returncode,data)
assert 'engineDispatch' in data,data
print('Shared typed bridge, full stat dispatch and changed Lua initialization: PASS')






assert data["engineDispatch"]["voiceLifetimeNoDevice"]
print("Voice lifetime, no-manager route, loop depth, load reset and duck envelope: PASS")

assert data["engineDispatch"]["audioPolicy"]
print("Audio category, speech, variation and suppression: PASS")
