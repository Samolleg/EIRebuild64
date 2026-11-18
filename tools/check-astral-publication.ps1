#requires -Version 7.6.10
#requires -PSEdition Core
param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$PublicationSource)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$ver=(Get-Content (Join-Path $vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
$vc=Join-Path $vs "VC/Tools/MSVC/$ver"
$sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sv=(Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path "$($_.FullName)/um/Windows.h"} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
$env:INCLUDE="$vc/include;$sdk/Include/$sv/ucrt;$sdk/Include/$sv/um;$sdk/Include/$sv/shared;$sdk/Include/$sv/winrt"
$env:LIB="$vc/lib/x64;$sdk/Lib/$sv/ucrt/x64;$sdk/Lib/$sv/um/x64"
$exe=Join-Path $out 'check-astral-publication.exe'
$cache=[IO.Path]::GetFullPath((Join-Path $repo '../../../builds/rebuild64/cache/work'))
if(!$PublicationSource){$PublicationSource="$repo/Game/OwnedRender/E1Terrain.cpp"}
$pakObjects=@(Get-ChildItem "$cache/out/obj/x64/Release/fig-to-skn" -File | Where-Object { $_.Name -eq "Pak.obj" -or $_.Name -like "zstd_*.obj" } | ForEach-Object FullName)
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /std:c++17 /EHsc /MT /O2 /Gy /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DEID3D11_EMBEDDED "/I$repo/external/owned-renderer/Source" "/Fo$out/" "/Fe$exe" "$repo/tools/check-astral-publication.cpp" $PublicationSource "$repo/external/owned-renderer/Source/owned_terrain_decode.cpp" "$repo/external/owned-renderer/Source/owned_asset_loader.cpp" "$repo/external/owned-renderer/Source/owned_resource_store.cpp" "$repo/external/owned-renderer/Source/owned_terrain_front_end.cpp" $pakObjects /link /OPT:REF d3d11.lib d3dcompiler.lib bcrypt.lib dxgi.lib dxguid.lib psapi.lib user32.lib gdi32.lib ole32.lib *> "$out/publication-compile.txt"
if($LASTEXITCODE -ne 0){Get-Content "$out/publication-compile.txt" -Tail 35;throw 'Publication test compile failed'}
