#requires -Version 7.6.10
#requires -PSEdition Core
param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$ShaderSource,[switch]$Hardware)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
if (!$ShaderSource) { $ShaderSource = Join-Path $repo 'external/owned-renderer/Source/renderer_native_d3d12.cpp' }
$source = [IO.File]::ReadAllText($ShaderSource).Replace("`r`n","`n")
$header = [IO.File]::ReadAllText((Join-Path $repo 'external/owned-renderer/Source/renderer.h')).Replace("`r`n","`n")
# Compile the shipping shader strings, not a copy of their implementation.
foreach ($shader in @(@('kRaytracingRepackShader','repack','rtRepackCS'),@('kRaytracingProbeShader','probe','rtProbeCS'))) {
    $start = $source.IndexOf('constexpr char '+$shader[0]+'[] =')
    if ($start -lt 0) { throw ('Missing shader '+$shader[0]) }
    $end = $source.IndexOf(";`n",$start)
    $block = $source.Substring($start,$end-$start)
    $hlsl = (@([regex]::Matches($block, '"(?:\\.|[^"\\])*"') | ForEach-Object { ConvertFrom-Json $_.Value }) -join '')
    if (!$hlsl) { throw 'Empty extracted shader' }
    [IO.File]::WriteAllText((Join-Path $out ($shader[1]+'.hlsl')),$hlsl)
}
# Use the actual CPU structure definitions, including members not read by RT.
$cpu = ''
foreach ($name in @('NativeWorldVertexGpu','NativeWorldStateGpu')) {
    $start = $header.IndexOf('struct '+$name+' {')
    if ($start -lt 0) { throw ('Missing CPU layout '+$name) }
    $end = $header.IndexOf("`n    };",$start)
    $cpu += $header.Substring($start,$end+7-$start)+"`n"
}
$start = $source.IndexOf('struct RaytracingRepackJob{')
$end = $source.IndexOf("`n};",$start)
$cpu += $source.Substring($start,$end+3-$start)+"`n"
[IO.File]::WriteAllText((Join-Path $out 'rt-cpu-layout.inc'),$cpu)
$project=$repo
while (!(Test-Path -LiteralPath (Join-Path $project 'RULES.md'))) {
    $project=Split-Path $project
    if (!$project) { throw 'Project root not found' }
}
$dxc=Join-Path $project 'deps/dxc/1.9.2609/bin/x64/dxc.exe'
foreach ($shader in @(@('repack','rtRepackCS'),@('probe','rtProbeCS'))) {
    & $dxc -T cs_6_6 -E $shader[1] -O3 -Fo (Join-Path $out ($shader[0]+'.dxil')) (Join-Path $out ($shader[0]+'.hlsl'))
    if ($LASTEXITCODE -ne 0) { throw 'RT shader compilation failed' }
}
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
    $exe=Join-Path $out 'check-raytracing-layout.exe'
    & "$vc/bin/Hostx64/x64/cl.exe" /nologo /std:c++17 /EHsc /MT /O2 /DNOMINMAX /DEID3D11_EMBEDDED "/I$out" "/Fo$out/" "/Fe$exe" "$repo/tools/check-raytracing-layout.cpp" /link d3d12.lib dxgi.lib *> "$out/compile.txt"
    if($LASTEXITCODE -ne 0){Get-Content "$out/compile.txt";throw 'RT layout check compile failed'}
    & $exe $out *> "$out/result.txt"
    Get-Content "$out/result.txt"
    if($LASTEXITCODE -ne 0){throw 'RT layout check failed'}
    if($Hardware){
        & $exe $out hardware *> "$out/hardware-result.txt"
        Get-Content "$out/hardware-result.txt"
        if($LASTEXITCODE -ne 0){throw 'Hardware RT layout check failed'}
    }
} finally {
    $env:INCLUDE=$savedInclude; $env:LIB=$savedLib
}
