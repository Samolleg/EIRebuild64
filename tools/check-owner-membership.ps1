#requires -Version 7.6.10
#requires -PSEdition Core
param([Parameter(Mandatory=$true)][string]$OutputDirectory, [string]$PreparedRoot)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project=$repo
while(!(Test-Path (Join-Path $project 'RULES.md'))){$project=Split-Path $project;if(!$project){throw 'Project root not found'}}
$cache=if($PreparedRoot){[IO.Path]::GetFullPath($PreparedRoot)}else{Join-Path $project 'builds/rebuild64/cache/work'}
$out=[IO.Path]::GetFullPath($OutputDirectory)
if($out.StartsWith($cache,[StringComparison]::OrdinalIgnoreCase)){throw 'Check output must be outside the prepared build'}
$buildLock=[IO.File]::Open((Join-Path (Split-Path $cache) '.build.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
trap { if($buildLock){$buildLock.Dispose()}; throw }
foreach($relative in 'Game/GameSave.cpp','Game/GameSave.h','Game/SaveSchema.cpp','Game/SaveSchema.h','Game/BaseManager.h','Game/Party.cpp','Game/Party.h','Game/Item.cpp','Game/Item.h','Game/GameBase.h','Game/SpellSystem.cpp'){
    if((Get-FileHash (Join-Path $repo $relative)).Hash -ne (Get-FileHash (Join-Path $cache $relative)).Hash){throw "Prepared core differs: $relative; prepare the matching build first"}
}
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$version=(Get-Content "$vs/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" -Raw).Trim()
$vc="$vs/VC/Tools/MSVC/$version"
$sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sv=(Get-ChildItem "$sdk/Include" -Directory | Where-Object {Test-Path "$($_.FullName)/um/Windows.h"} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
$env:INCLUDE="$vc/include;$vc/atlmfc/include;$sdk/Include/$sv/ucrt;$sdk/Include/$sv/um;$sdk/Include/$sv/shared;$sdk/Include/$sv/winrt"
$env:LIB="$vc/lib/x64;$vc/atlmfc/lib/x64;$sdk/Lib/$sv/ucrt/x64;$sdk/Lib/$sv/um/x64"
$env:PATH="$vc/bin/Hostx64/x64;$sdk/bin/$sv/x64;$env:PATH"
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /c /std:c++17 /EHsc /MT /O2 /Gy /DNDEBUG /DDT_POLYREF64 /DWIN32 /D__GAME__ /DBRANCH_DX11 /DEID3D11_EMBEDDED /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0A00 "/I$repo" "/I$repo/external/owned-renderer/Source" "/I$cache/out/include" "/I$cache/out/x64/Release/luajit/include" "/I$repo/external/recastnavigation/Detour/Include" "/Fo$out/check-owner-membership.obj" "$repo/tools/check-owner-membership.cpp" *> "$out/compile.txt"
if($LASTEXITCODE -ne 0){Get-Content "$out/compile.txt" -Tail 20;throw 'compile failed'}
$lines=Get-Content "$cache/out/obj/x64/Release/EI_Rebuild/EI_Rebuild.tlog/link.command.1.tlog"
$argsText=($lines | Select-Object -Skip 1) -join "`r`n"
$argsText=[regex]::Replace($argsText,'/OUT:"[^"]+"',('/OUT:"'+$out+'/check-owner-membership.exe"'))
$argsText=[regex]::Replace($argsText,'/PDB:"[^"]+"',('/PDB:"'+$out+'/check-owner-membership.pdb"'))
$argsText=[regex]::Replace($argsText,'/IMPLIB:"[^"]+"',('/IMPLIB:"'+$out+'/check-owner-membership.lib"'))
$argsText=[regex]::Replace($argsText,'/LTCGOUT:"[^"]+"','')
$argsText=$argsText.Replace('/LTCG:incremental','/LTCG').Replace('/SUBSYSTEM:WINDOWS','/SUBSYSTEM:CONSOLE')
$argsText+=' "'+$out+'/check-owner-membership.obj"'
    & "$vc/bin/Hostx64/x64/cl.exe" /nologo /c /std:c++17 /EHsc /MT /O2 /Gy /DNDEBUG /DDT_POLYREF64 /DWIN32 /D__GAME__ /DBRANCH_DX11 /DEID3D11_EMBEDDED /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0A00 "/I$repo" "/I$repo/external/owned-renderer/Source" "/I$cache/out/include" "/I$cache/out/x64/Release/luajit/include" "/I$repo/external/recastnavigation/Detour/Include" "/Fo$out/BaseManager.obj" "$repo/Game/BaseManager.cpp" *> "$out/base-compile.txt"
    if($LASTEXITCODE -ne 0){Get-Content "$out/base-compile.txt" -Tail 12;throw 'BaseManager compile failed'}
    $argsText=$argsText.Replace(($cache+'/out/obj/x64/Release/EI_Rebuild/BaseManager.obj').Replace('/','\').ToUpperInvariant(),('"'+$out+'/BaseManager.obj"'))
[IO.File]::WriteAllText("$out/link.rsp",$argsText)
Push-Location $cache
try { & "$vc/bin/Hostx64/x64/link.exe" "@$out/link.rsp" *> "$out/link.txt" } finally { Pop-Location }
if($LASTEXITCODE -ne 0){Get-Content "$out/link.txt" -Tail 20;throw 'link failed'}
$env:PATH=(Join-Path $project 'builds/rebuild64/current')+';'+$env:PATH
& "$out/check-owner-membership.exe" *> "$out/result.txt"
Get-Content "$out/result.txt"
$result=$LASTEXITCODE
$buildLock.Dispose()
exit $result
