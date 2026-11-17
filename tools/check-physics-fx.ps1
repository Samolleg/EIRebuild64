param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$version=(Get-Content "$vs/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" -Raw).Trim()
$vc="$vs/VC/Tools/MSVC/$version"
$sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sv=(Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path "$($_.FullName)/um/Windows.h"} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
$savedInclude=$env:INCLUDE;$savedLib=$env:LIB
try {
    & "$vs/MSBuild/Current/Bin/MSBuild.exe" "$repo/tools/jolt/Jolt.vcxproj" /nologo /v:minimal /m:4 /nr:false /p:Configuration=Release /p:Platform=x64 *> "$out/jolt-build.txt"
    if($LASTEXITCODE -ne 0){Get-Content "$out/jolt-build.txt" -Tail 24;throw 'Jolt build failed'}
    $env:INCLUDE="$repo/third_party/jolt;$repo/external/owned-renderer/Source;$vc/include;$sdk/Include/$sv/ucrt;$sdk/Include/$sv/um;$sdk/Include/$sv/shared;$sdk/Include/$sv/winrt"
    $env:LIB="$vc/lib/x64;$sdk/Lib/$sv/ucrt/x64;$sdk/Lib/$sv/um/x64"
    $exe=Join-Path $out 'check-physics-fx.exe'
    Push-Location $out
    try {
        & "$vc/bin/Hostx64/x64/cl.exe" /nologo /std:c++17 /EHsc /MT /O2 /DNDEBUG /DNOMINMAX "/Fe$exe" "$repo/tools/check-physics-fx.cpp" "$repo/Game/PhysicsFxCore.cpp" "$repo/out/x64/Release/Jolt/Jolt.lib" *> "$out/compile.txt"
        if($LASTEXITCODE -ne 0){Get-Content "$out/compile.txt";throw 'Physics check compile failed'}
        & $exe "$out/trajectory.csv" *> "$out/result.txt"
        Get-Content "$out/result.txt" -Tail 12
        if($LASTEXITCODE -ne 0){throw 'Physics check failed'}
    } finally {Pop-Location}
} finally {$env:INCLUDE=$savedInclude;$env:LIB=$savedLib}
