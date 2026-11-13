param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC x64 tools not found' }
$version = (Get-Content (Join-Path $vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
$vc = Join-Path $vs "VC/Tools/MSVC/$version"
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sv = (Get-ChildItem (Join-Path $sdk 'Include') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um/Windows.h') } | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
$env:INCLUDE = "$vc/include;$sdk/Include/$sv/ucrt;$sdk/Include/$sv/um;$sdk/Include/$sv/shared"
$env:LIB = "$vc/lib/x64;$sdk/Lib/$sv/ucrt/x64;$sdk/Lib/$sv/um/x64"
foreach ($name in 'CL','_CL_','LINK','_LINK_') { [Environment]::SetEnvironmentVariable($name,$null,'Process') }
$exe = Join-Path $out 'check-gamepad.exe'
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /std:c++17 /EHsc /MT /W4 /WX (Join-Path $repo 'tools/check-gamepad.cpp') "/Fo$out/check-gamepad.obj" "/Fe$exe" *> (Join-Path $out 'compile.txt')
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $out 'compile.txt'); throw 'Gamepad test compilation failed' }
& $exe *> (Join-Path $out 'result.txt')
$result = $LASTEXITCODE
Get-Content (Join-Path $out 'result.txt')
if ($result -ne 0) { throw 'Gamepad regression failed' }
