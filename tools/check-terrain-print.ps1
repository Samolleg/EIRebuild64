param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $out | Out-Null
$vs=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$version=(Get-Content "$vs/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" -Raw).Trim();$vc="$vs/VC/Tools/MSVC/$version"
$sdk="${env:ProgramFiles(x86)}/Windows Kits/10"
$sv=(Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path "$($_.FullName)/um/Windows.h"} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
try{
 $env:INCLUDE="$repo/external/owned-renderer/Source;$vc/include;$sdk/Include/$sv/ucrt;$sdk/Include/$sv/um;$sdk/Include/$sv/shared"
 $env:LIB="$vc/lib/x64;$sdk/Lib/$sv/ucrt/x64;$sdk/Lib/$sv/um/x64"
 Push-Location $out
 try{
  & "$vc/bin/Hostx64/x64/cl.exe" /nologo /std:c++17 /EHsc /MT /O2 /DNDEBUG "$repo/tools/check-terrain-print.cpp" /Fecheck-terrain-print.exe /link d3dcompiler.lib *> compile.txt
  if($LASTEXITCODE -ne 0){Get-Content compile.txt;throw 'Terrain-print check compilation failed'}
  & ./check-terrain-print.exe *> result.txt
  if($LASTEXITCODE -ne 0){Get-Content result.txt;throw 'Terrain-print shader check failed'}
  Get-Content result.txt
 }finally{Pop-Location}
}finally{$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
