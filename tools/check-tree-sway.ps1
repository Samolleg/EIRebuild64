param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$projectRoot = $repo
while (!(Test-Path -LiteralPath (Join-Path $projectRoot 'RULES.md'))) {
    $parent = Split-Path $projectRoot
    if (!$parent -or $parent -eq $projectRoot) { throw 'Project root not found' }
    $projectRoot = $parent
}
$cache = Join-Path $projectRoot 'builds/rebuild64/cache/work'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC not found' }
$version = (Get-Content (Join-Path $vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
$vc = Join-Path $vs "VC/Tools/MSVC/$version"
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkVersion = (Get-ChildItem (Join-Path $sdk 'Include') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um/Windows.h') } | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
$env:INCLUDE = "$vc/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/winrt"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
foreach ($name in 'CL','_CL_','LINK','_LINK_') { [Environment]::SetEnvironmentVariable($name,$null,'Process') }
$renderer = Join-Path $cache 'out/x64/Release/OwnedRenderer/OwnedRenderer.lib'
$exe = Join-Path $out 'check-tree-sway.exe'
$arguments = @('/nologo','/std:c++17','/EHsc','/MT','/O2','/Gy','/GL','/W3','/DNOMINMAX','/DDIRECTDRAW_VERSION=0x0700','/DDIRECT3D_VERSION=0x0700','/DEID3D11_EMBEDDED','/D_CRT_SECURE_NO_WARNINGS',
    ('/I'+$repo),('/I'+(Join-Path $repo 'external/owned-renderer/Source')),('/I'+(Join-Path $repo 'external/owned-renderer/ThirdParty/imgui')),
    ('/Fo'+$out+'\'),('/Fe'+$exe),('/Fd'+(Join-Path $out 'check.pdb')))
$arguments += @('tools/check-tree-sway.cpp','Game/OwnedRender/E1Surface.cpp','external/owned-renderer/Source/retained_static_submission.cpp','external/owned-renderer/Source/owned_asset_loader.cpp','external/owned-renderer/Source/owned_resource_store.cpp','external/owned-renderer/Source/owned_visual_evaluation.cpp','external/owned-renderer/Source/owned_terrain_front_end.cpp','external/owned-renderer/Source/surface_upload.cpp' | ForEach-Object { Join-Path $repo $_ })
$nativeObjects = Join-Path $cache 'out/obj/x64/Release/EI_Rebuild'
$arguments += Join-Path $nativeObjects 'Pak.obj'
$arguments += @(Get-ChildItem -LiteralPath $nativeObjects -Filter 'zstd_*.obj' -File | ForEach-Object FullName)
$arguments += @('/link','/OPT:REF','/LTCG','/SUBSYSTEM:CONSOLE','user32.lib','shell32.lib','bcrypt.lib','cabinet.lib','d3d11.lib','dxgi.lib')
& (Join-Path $vc 'bin/Hostx64/x64/cl.exe') @arguments *> (Join-Path $out 'compile.txt')
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $out 'compile.txt') | Select-Object -Last 30; throw 'Tree sway check compilation failed' }
& $exe *> (Join-Path $out 'result.txt')
$code=$LASTEXITCODE
Get-Content (Join-Path $out 'result.txt')
if($code -ne 0){throw 'Tree sway check failed'}