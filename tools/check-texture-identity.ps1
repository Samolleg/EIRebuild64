#requires -Version 7.6.10
#requires -PSEdition Core
param([Parameter(Mandatory=$true)][string]$OutputDirectory,
      [Parameter(Mandatory=$true)][string]$ZstdRoot)
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
$savedInclude=$env:INCLUDE; $savedLib=$env:LIB
try {
    $env:INCLUDE="$vc/include;$sdk/Include/$sv/ucrt;$sdk/Include/$sv/um;$sdk/Include/$sv/shared;$sdk/Include/$sv/winrt"
    $env:LIB="$vc/lib/x64;$sdk/Lib/$sv/ucrt/x64;$sdk/Lib/$sv/um/x64"
    $source=Join-Path $repo 'external/owned-renderer/Source'
    $zstd=[IO.Path]::GetFullPath((Join-Path $ZstdRoot 'lib'))
    $zstdSources=@(Get-ChildItem "$zstd/common/*.c","$zstd/compress/*.c","$zstd/decompress/*.c" | ForEach-Object FullName)
    if (!$zstdSources.Count) { throw 'Pinned zstd sources are missing' }
    $exe=Join-Path $out 'check-texture-identity.exe'
    New-Item -ItemType Directory -Force -Path "$out/include/nlohmann" | Out-Null
    Copy-Item "$repo/SDKs/JSON/*" "$out/include/nlohmann" -Recurse -Force
    # Link the production key functions; discard unrelated renderer/store code.
    & "$vc/bin/Hostx64/x64/cl.exe" /nologo /std:c++17 /EHsc /MT /O2 /Gy /Gw /DNOMINMAX /DEID3D11_EMBEDDED "/I$source" "/I$zstd" "/I$out/include" "/Fo$out/" "/Fe$exe" "$repo/tools/check-texture-identity.cpp" "$source/native_prebuilt.cpp" "$source/owned_resource_store.cpp" "$repo/Pak/Pak.cpp" @zstdSources /link /OPT:REF /OPT:ICF *> "$out/compile.txt"
    if($LASTEXITCODE -ne 0){Get-Content "$out/compile.txt";throw 'Texture identity check compile failed'}
    & $exe *> "$out/result.txt"
    Get-Content "$out/result.txt"
    if($LASTEXITCODE -ne 0){throw 'Texture identity check failed'}
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib
}
