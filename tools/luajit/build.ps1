#requires -Version 7.6.10
#requires -PSEdition Core
param([string]$VSInstallDir, [switch]$Force)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vendor = Join-Path $repo 'third_party/luajit'
$intermediate = Join-Path $repo 'out/obj/x64/Release/luajit'
$output = Join-Path $repo 'out/x64/Release/luajit'
$library = Join-Path $output 'luajit.lib'
$inputs = @(Get-ChildItem -LiteralPath $vendor -File -Recurse) +
          @(Get-ChildItem -LiteralPath $PSScriptRoot -File)
if (!$Force -and (Test-Path -LiteralPath $library)) {
    $newest = ($inputs | Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum
    if ((Get-Item -LiteralPath $library).LastWriteTimeUtc -ge $newest) {
        Write-Output "LuaJIT is up to date: $library"
        exit 0
    }
}
if (!$VSInstallDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $VSInstallDir = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$VSInstallDir) { throw 'MSVC x64 tools not found.' }
}
$vcvars = Join-Path ([IO.Path]::GetFullPath($VSInstallDir)) 'VC/Auxiliary/Build/vcvars64.bat'
if (!(Test-Path -LiteralPath $vcvars)) { throw "Missing vcvars64.bat: $vcvars" }
New-Item -ItemType Directory -Force -Path $intermediate,$output | Out-Null
$sourceCopy = Join-Path $intermediate 'source'
New-Item -ItemType Directory -Force -Path $sourceCopy | Out-Null
Copy-Item -LiteralPath (Join-Path $vendor 'src'),(Join-Path $vendor 'dynasm'),(Join-Path $vendor '.relver') -Destination $sourceCopy -Recurse -Force

# Use the pinned upstream build steps. Adapt only this private generated copy:
# /MT for the static library and build tools, explicit GC64 request, library-only
# completion, and an actual nonzero exit code when upstream reports failure.
$batchPath = Join-Path $sourceCopy 'src/msvcbuild.bat'
$batch = [IO.File]::ReadAllText($batchPath).Replace("`r`n", "`n")
$batch = $batch.Replace('@set LJCOMPILE=cl /nologo /c /O2 /W3 ', ('@set LJCOMPILE=cl /nologo /c /O2 /W3 /MT /MP /FS /Fd"' + (Join-Path $output 'luajit.compiler.pdb') + '" /DLUAJIT_ENABLE_GC64 '))
$start = $batch.IndexOf("`n:MTDLL`n") + 1
$end = $batch.IndexOf("`n:SETHOSTVARS`n") + 1
if ($start -lt 0 -or $end -le $start) { throw 'Pinned msvcbuild layout changed.' }
$batch = $batch.Substring(0, $start) + ":MTDLL`n@exit /b 0`n" + $batch.Substring($end)
$batch = $batch.Replace(":BAD`n@echo.", ":BAD`n@exit /b 1`n@echo.")
[IO.File]::WriteAllText($batchPath, $batch.Replace("`n", "`r`n"), [Text.Encoding]::ASCII)
$driver = @"
@echo off
setlocal
rem Do not inherit compiler switches from an unrelated developer shell.
set CL=
set _CL_=
set LINK=
set _LINK_=
rem Explicit tool directories also support NoDefaultCurrentDirectoryInExePath=1.
set "PATH=%LUAJIT_BUILD_SRC%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%LUAJIT_BUILD_VCVARS%" >nul
if errorlevel 1 exit /b 1
cd /d "%LUAJIT_BUILD_SRC%"
if errorlevel 1 exit /b 1
call ".\msvcbuild.bat" static
exit /b %errorlevel%
"@
$driverPath = Join-Path $intermediate 'build.cmd'
# Expand Unicode paths from environment variables, independently of cmd's code page.
[IO.File]::WriteAllText($driverPath, $driver, [Text.Encoding]::ASCII)
$env:LUAJIT_BUILD_VCVARS = $vcvars
$env:LUAJIT_BUILD_SRC = Join-Path $sourceCopy 'src'
& $env:ComSpec /d /c "`"$driverPath`""
if ($LASTEXITCODE -ne 0) { throw "LuaJIT build failed ($LASTEXITCODE)." }
Copy-Item -LiteralPath (Join-Path $sourceCopy 'src/lua51.lib') -Destination $library -Force
$include = Join-Path $output 'include'
New-Item -ItemType Directory -Force -Path $include | Out-Null
foreach ($header in 'lua.h','lua.hpp','lauxlib.h','lualib.h','luaconf.h','luajit.h') {
    Copy-Item -LiteralPath (Join-Path $sourceCopy "src/$header") -Destination $include -Force
}
Copy-Item -LiteralPath (Join-Path $sourceCopy 'src/jit') -Destination $output -Recurse -Force
Copy-Item -LiteralPath (Join-Path $vendor 'COPYRIGHT') -Destination $output -Force
Write-Output "Built static LuaJIT x64 GC64 (/MT): $library"
