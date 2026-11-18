#requires -Version 7.6.10
#requires -PSEdition Core
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
$objects = @(Get-ChildItem (Join-Path $cache 'out/obj/x64/Release/EI_Rebuild') -File | Where-Object { $_.Name -match '^(Recast|Detour).*\.obj$' } | ForEach-Object FullName)
$renderer = Join-Path $cache 'out/x64/Release/OwnedRenderer/OwnedRenderer.lib'
if ($objects.Count -lt 10 -or !(Test-Path -LiteralPath $renderer)) { throw 'Prepared native build cache required' }
$exe = Join-Path $out 'check-diagnostics-off.exe'
$arguments = @('/nologo','/std:c++17','/EHsc','/MT','/O2','/Gy','/W3','/DNOMINMAX','/DDT_POLYREF64','/DEID3D11_EMBEDDED','/D_CRT_SECURE_NO_WARNINGS',
    ('/I'+$repo),('/I'+(Join-Path $repo 'SDKs/dx7sdk/include')),('/I'+(Join-Path $repo 'external/owned-renderer/Source')),
    ('/Fo'+$out+'\'),('/Fe'+$exe),('/Fd'+(Join-Path $out 'check.pdb')))
$arguments += @('tools/check-diagnostics-off.cpp','Navigation/Build.cpp','Navigation/Geometry.cpp','Navigation/Query.cpp' | ForEach-Object { Join-Path $repo $_ })
$arguments += $objects
$arguments += @($renderer,'/link','/OPT:REF','/LTCG','/SUBSYSTEM:CONSOLE','user32.lib','shell32.lib','bcrypt.lib','cabinet.lib','d3d11.lib','dxgi.lib')
& (Join-Path $vc 'bin/Hostx64/x64/cl.exe') @arguments *> (Join-Path $out 'compile.txt')
if ($LASTEXITCODE -ne 0) {
    Select-String -LiteralPath (Join-Path $out 'compile.txt') -Pattern 'error|LNK\d+' | Select-Object -First 20
    throw 'Diagnostics regression compilation failed'
}
foreach ($mode in 'off','on','background') {
    $flags = @()
    if ($mode -eq 'on') { $flags = @('--diagnostics') }
    if ($mode -eq 'background') { $flags = @('--background') }
    & $exe @flags *> (Join-Path $out ($mode+'.txt'))
    $code = $LASTEXITCODE
    Get-Content -LiteralPath (Join-Path $out ($mode+'.txt')) | Select-Object -Last 8
    if ($code -ne 0) { throw "Diagnostics regression failed: $mode" }
}
