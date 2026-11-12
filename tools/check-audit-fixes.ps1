param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$ModConfig,
    [string[]]$ResourcePaths=@()
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC x64 tools not found'}
$version=(Get-Content -LiteralPath (Join-Path $vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
$vc=Join-Path $vs ('VC/Tools/MSVC/'+$version)
$sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkVersion=(Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'um/Windows.h')} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
if(!$sdkVersion){throw 'Windows SDK not found'}
$env:INCLUDE="$vc/include;$vc/atlmfc/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/winrt"
$env:LIB="$vc/lib/x64;$vc/atlmfc/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
$compiler=Join-Path $vc 'bin/Hostx64/x64/cl.exe'
$env:PATH="$vc/bin/Hostx64/x64;$sdk/bin/$sdkVersion/x64;"+$env:PATH
foreach($name in 'CL','_CL_','LINK','_LINK_'){[Environment]::SetEnvironmentVariable($name,$null,'Process')}
$exe=Join-Path $out 'check-audit-fixes.exe'
$sources=@('tools/check-audit-fixes.cpp','Game/CResourceBase.cpp','Game/CMappedResource.cpp','Game/CMMIOResource.cpp','Game/Win32Classes.cpp','Game/FileRegistry.cpp','Game/OwnedRender/ResourceMapping.cpp')
$args=@('/nologo','/std:c++17','/EHsc','/MT','/O2','/Gy','/GS','/W3','/DNDEBUG','/DNOMINMAX','/DWIN32','/D_WIN32_WINNT=0x0A00','/D_CRT_SECURE_NO_WARNINGS','/D_CRT_NONSTDC_NO_WARNINGS',
    ('/I'+$repo),('/I'+(Join-Path $repo 'Game')),('/I'+(Join-Path $repo 'external/owned-renderer/Source')),('/I'+(Join-Path $out 'include')),('/I'+(Join-Path $repo 'third_party/luajit/src')),
    ('/Fo'+$out+'\'),('/Fe'+$exe),('/Fd'+(Join-Path $out 'check-audit-fixes.pdb')))
# Use the repository's pinned JSON headers, without package restore or the game PCH.
$json=Join-Path $out 'include/nlohmann'
New-Item -ItemType Directory -Force -Path $json | Out-Null
Get-ChildItem -LiteralPath (Join-Path $repo 'SDKs/JSON') | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $json -Recurse -Force}
# Game headers require the generated public LuaJIT header from the prepared build.
# No Lua VM is linked or run by this check.
$projectRoot=$repo
while(!(Test-Path -LiteralPath (Join-Path $projectRoot 'RULES.md'))){
    $parent=Split-Path $projectRoot
    if(!$parent -or $parent -eq $projectRoot){throw 'Project root not found'}
    $projectRoot=$parent
}
$luaHeader=Join-Path $projectRoot 'builds/rebuild64/cache/work/out/x64/Release/luajit/include/luajit.h'
if(!(Test-Path -LiteralPath $luaHeader)){throw 'Run the standard build preparation before these checks (generated luajit.h required)'}
Copy-Item -LiteralPath $luaHeader -Destination (Join-Path $out 'include/luajit.h') -Force
$args+=@($sources|ForEach-Object{Join-Path $repo $_})
# The production mapped-resource reader now supports PAK as well as RES.
# Reuse the prepared native PAK/zstd objects; these are read-only inputs.
$nativeObjects=Join-Path $projectRoot 'builds/rebuild64/cache/work/out/obj/x64/Release/EI_Rebuild'
$pakObject=Join-Path $nativeObjects 'Pak.obj'
if(!(Test-Path -LiteralPath $pakObject)){throw 'Prepared PAK object required'}
$args+=$pakObject
$args+=@(Get-ChildItem -LiteralPath $nativeObjects -Filter 'zstd_*.obj' -File | ForEach-Object FullName)
$args+=@('/link','/OPT:REF','/SUBSYSTEM:CONSOLE','winmm.lib','psapi.lib')
& $compiler @args *> (Join-Path $out 'compile.txt')
if($LASTEXITCODE -ne 0){Select-String -LiteralPath (Join-Path $out 'compile.txt') -Pattern ': error|fatal error|LNK\d+' | Select-Object -First 25;throw 'Audit regression check compilation failed'}
& $exe (Join-Path $out 'fixtures') $ModConfig @ResourcePaths *> (Join-Path $out 'result.txt')
$result=$LASTEXITCODE
Get-Content -LiteralPath (Join-Path $out 'result.txt')
if($result -ne 0){throw "Audit regression checks failed ($result)"}
