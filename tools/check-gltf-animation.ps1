param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$Glb,[string]$Profile)
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
    $storage=Join-Path $out 'check-gltf-storage.exe'
    & "$vc/bin/Hostx64/x64/cl.exe" /nologo /std:c++17 /EHsc /MT /O2 /DNOMINMAX "/I$repo/external/owned-renderer/Source" "/Fo$out/" "/Fe$storage" "$repo/tools/check-gltf-storage.cpp" *> "$out/storage-compile.txt"
    if($LASTEXITCODE -ne 0){Get-Content "$out/storage-compile.txt";throw 'glTF storage check compile failed'}
    & $storage *> "$out/storage-result.txt"
    Get-Content "$out/storage-result.txt"
    if($LASTEXITCODE -ne 0){throw 'glTF storage check failed'}
    $exe=Join-Path $out 'check-gltf-animation.exe'
    $extra=@()
    if($Glb){
        if(!$Profile){throw 'The real GLB check requires -Profile'}
        New-Item -ItemType Directory -Force -Path "$out/include/nlohmann" | Out-Null
        Copy-Item "$repo/SDKs/JSON/*" "$out/include/nlohmann" -Recurse -Force
        $extra=@('/DCHECK_GLTF_ASSET',"/I$out/include","$repo/external/owned-renderer/Source/gltf_decode.cpp")
    }
    & "$vc/bin/Hostx64/x64/cl.exe" /nologo /std:c++17 /EHsc /MT /O2 /Gy /Gw /DNOMINMAX "/I$repo/external/owned-renderer/Source" "/Fo$out/" "/Fe$exe" "$repo/tools/check-gltf-animation.cpp" "$repo/external/owned-renderer/Source/owned_visual_evaluation.cpp" @extra /link /OPT:REF d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib windowscodecs.lib ole32.lib *> "$out/compile.txt"
    if($LASTEXITCODE -ne 0){Get-Content "$out/compile.txt";throw 'glTF animation check compile failed'}
    if($Glb){& $exe $Glb $Profile *> "$out/result.txt"}else{& $exe *> "$out/result.txt"}
    Get-Content "$out/result.txt"
    if($LASTEXITCODE -ne 0){throw 'glTF animation check failed'}
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib
}
