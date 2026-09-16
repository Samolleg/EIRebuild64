"""Build the isolated EIRebuild x86 tree with a case-normalized Windows environment."""
import os
import pathlib
import subprocess
import sys
import shutil

root = pathlib.Path(__file__).resolve().parent
# Restore the bundled JSON library's expected nlohmann include layout.
shutil.copytree(root / "SDKs" / "JSON", root / "out" / "include" / "nlohmann", dirs_exist_ok=True)
label = sys.argv[1] if len(sys.argv) > 1 else "build"
env = {key.upper(): value for key, value in os.environ.items()}
env['Path'] = env.pop('PATH', '')
env['MSBUILDDISABLENODEREUSE'] = '1'
msbuild = pathlib.Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe")
evidence = root / "evidence"
evidence.mkdir(exist_ok=True)
with (evidence / f"{label}-console.log").open("wb") as log:
    result = subprocess.run([str(msbuild), str(root / "EI_Rebuild.sln"), "/t:Build",
        "/p:Configuration=Release", "/p:Platform=x86", "/m:1", "/nr:false", "/v:minimal", "/nologo",
        "/fl", f"/flp:logfile={evidence / (label + '.log')};verbosity=normal"],
        cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT)
print(f"Build exit={result.returncode}; log={evidence / (label + '-console.log')}")
sys.exit(result.returncode)
