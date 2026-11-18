#requires -Version 7.6.10
#requires -PSEdition Core
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$RtxdiRoot
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'sdk-files.ps1')
$fixture = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ('sdk [fixture] ' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixture)
foreach ($name in @('Include/a/first.h','Include/a/deep/second.h','Include/b/shared.h','Include/b/readme.txt','bin/exact.dll','bin/child/exact.dll','root.h','nested/extra.h','LICENSE')) {
    $path = Join-Path $fixture $name
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $path))
    [IO.File]::WriteAllText($path, $name)
}
[void][IO.Directory]::CreateDirectory((Join-Path $fixture 'empty'))
function Check-Names($Pattern, $Expected) {
    $actual = @(Get-LocalSdkFiles $fixture $Pattern | ForEach-Object { $_.FullName.Substring($fixture.Length+1).Replace('\','/') } | Sort-Object)
    if (($actual -join '|') -cne (($Expected | Sort-Object) -join '|')) { throw "Wrong selection for $Pattern : $actual" }
}
Check-Names 'Include/*' @('Include/a/first.h','Include/a/deep/second.h','Include/b/shared.h','Include/b/readme.txt')
Check-Names 'Include/*.h' @('Include/a/first.h','Include/a/deep/second.h','Include/b/shared.h')
Check-Names 'bin/exact.dll' @('bin/exact.dll')
Check-Names 'LICENSE' @('LICENSE')
Check-Names 'empty/*' @()
foreach ($pattern in @('missing.dll','missing/*','Include')) {
    $failed = $false
    try { $null = @(Get-LocalSdkFiles $fixture $pattern) } catch { $failed = $true }
    if (!$failed) { throw "Missing or invalid input accepted: $pattern" }
}
$actual = @(Get-LocalSdkFiles $RtxdiRoot 'Include/*' | ForEach-Object FullName | Sort-Object)
$expected = @([IO.Directory]::GetFiles((Join-Path $RtxdiRoot 'Include'), '*', [IO.SearchOption]::AllDirectories) | Sort-Object)
if (!$expected.Count -or ($actual -join '|') -cne ($expected -join '|')) { throw 'Incomplete real RTXDI tree' }
$manifest = @($actual | ForEach-Object { (Get-FileHash -LiteralPath $_).Hash + '  ' + $_.Substring($RtxdiRoot.TrimEnd('\','/').Length+1).Replace('\','/') })
[IO.File]::WriteAllLines((Join-Path $OutputDirectory 'rtxdi-files.sha256'), [string[]]$manifest)
Write-Output "PASS PowerShell $($PSVersionTable.PSVersion): nested-only SDK, filters, exact files, literal directories, missing inputs; RTXDI $($actual.Count) files."
