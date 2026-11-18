#requires -Version 7.6.10
#requires -PSEdition Core
param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$SourceCases)
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
    $project=$repo;while(!(Test-Path "$project/RULES.md")){$project=Split-Path $project;if(!$project){throw 'Project root missing'}}
    & "$repo/tools/physx/build.ps1" -ProjectRoot $project
    $physx="$project/deps/physx/5.6.1/sdk"
    $env:INCLUDE="$physx/include;$repo/external/owned-renderer/Source;$vc/include;$sdk/Include/$sv/ucrt;$sdk/Include/$sv/um;$sdk/Include/$sv/shared;$sdk/Include/$sv/winrt"
    $env:LIB="$vc/lib/x64;$sdk/Lib/$sv/ucrt/x64;$sdk/Lib/$sv/um/x64"
    $exe=Join-Path $out 'check-physics-fx.exe'
    Push-Location $out
    try {
        $libs=@(Get-ChildItem "$physx/lib" -Filter '*.lib' -File | ForEach-Object FullName)
        & "$vc/bin/Hostx64/x64/cl.exe" /nologo /std:c++17 /EHsc /MT /O2 /DNDEBUG /DNOMINMAX /DPX_PHYSX_STATIC_LIB "/Fe$exe" "$repo/tools/check-physics-fx.cpp" "$repo/Game/PhysicsFxPhysX.cpp" @libs /link user32.lib advapi32.lib *> "$out/compile.txt"
        if($LASTEXITCODE -ne 0){Get-Content "$out/compile.txt";throw 'Physics check compile failed'}
        Copy-Item "$physx/bin/PhysXGpu_64.dll" $out -Force
        $checkArgs=@("$out/trajectory.csv");if($SourceCases){$checkArgs+=[IO.Path]::GetFullPath($SourceCases)}
        & $exe @checkArgs *> "$out/result.txt"
        Get-Content "$out/result.txt" -Tail 12
        if($LASTEXITCODE -ne 0){throw 'Physics check failed'}
        $negative=Join-Path $out 'missing-runtime';New-Item -ItemType Directory -Force $negative | Out-Null
        if(Test-Path "$negative/PhysXGpu_64.dll"){throw 'Missing-runtime fixture contaminated'}
        Copy-Item $exe $negative -Force
        & "$negative/check-physics-fx.exe" --expect-gpu-unavailable *> "$out/missing-runtime.txt"
        Get-Content "$out/missing-runtime.txt"
        if($LASTEXITCODE -ne 0){throw 'Missing GPU runtime check failed'}
    } finally {Pop-Location}
} finally {$env:INCLUDE=$savedInclude;$env:LIB=$savedLib}
