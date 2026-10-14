"""Restore dependencies and build EIRebuild Release x86 on Windows (Python 3.8+)."""
import os
import pathlib
import subprocess
import sys
import shutil
import urllib.request

root = pathlib.Path(__file__).resolve().parent
# Restore the bundled JSON library's expected nlohmann include layout.
shutil.copytree(root / "SDKs" / "JSON", root / "out" / "include" / "nlohmann", dirs_exist_ok=True)
label = sys.argv[1] if len(sys.argv) > 1 else "build"
env = {key.upper(): value for key, value in os.environ.items()}
env['Path'] = env.pop('PATH', '')
env['MSBUILDDISABLENODEREUSE'] = '1'
if os.name != 'nt':
    sys.exit('Build requires Windows and Visual Studio 2022 C++/MFC v143.')
vswhere = pathlib.Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
if not vswhere.is_file():
    sys.exit('Install Visual Studio 2022 or Build Tools with C++ tools, v143 MFC and Windows SDK.')
installation = subprocess.check_output([
    str(vswhere), '-latest', '-products', '*', '-version', '[17.0,18.0)',
    '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    'Microsoft.VisualStudio.Component.VC.ATLMFC', '-property', 'installationPath',
], env=env, encoding='utf-8').strip()
if not installation:
    sys.exit('Visual Studio 2022 C++ tools and MFC for latest v143 x86/x64 are required.')
msbuild = pathlib.Path(installation) / 'MSBuild/Current/Bin/MSBuild.exe'
cache = root / 'out/tools'
cache.mkdir(parents=True, exist_ok=True)
nuget = cache / 'nuget-6.12.1.exe'
if not nuget.is_file():
    print('Downloading NuGet 6.12.1 from dist.nuget.org...', flush=True)
    temporary = nuget.with_suffix('.download')
    urllib.request.urlretrieve('https://dist.nuget.org/win-x86-commandline/v6.12.1/nuget.exe', temporary)
    temporary.replace(nuget)
subprocess.run([
    str(nuget), 'restore', str(root / 'packages.config'),
    '-PackagesDirectory', str(root / 'packages'),
    '-Source', 'https://api.nuget.org/v3/index.json', '-NonInteractive',
], cwd=root, env=env, check=True)
if pathlib.Path(label).name != label or label in ('.', '..'):
    sys.exit('Build label must be a filename, not a path.')
evidence = root / "evidence"
evidence.mkdir(exist_ok=True)
print(f'Building Release x86. Log: {evidence / (label + "-console.log")}', flush=True)
with (evidence / f"{label}-console.log").open("wb") as log:
    result = subprocess.run([str(msbuild), str(root / "EI_Rebuild.sln"), "/t:Build",
        "/p:Configuration=Release", "/p:Platform=x86", "/m:1", "/nr:false", "/v:minimal", "/nologo",
        "/fl", f"/flp:logfile={evidence / (label + '.log')};verbosity=normal"],
        cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT)
print(f"Build exit={result.returncode}; log={evidence / (label + '-console.log')}")
if result.returncode == 0:
    shutil.copy2(root / 'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe',
                 root / 'out/Win32/Release/EIRebuild.exe')
    print(f'Output: {root / "out/Win32/Release/EIRebuild.exe"}')
sys.exit(result.returncode)
