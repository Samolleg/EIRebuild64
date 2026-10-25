# Writes an nmake dependency file for incremental builds (build-local-x86.cmd,
# Host\build.cmd): one "object: header" line per header each object included
# at its last compile (cl /sourceDependencies JSON next to the object),
# project headers only, and only headers that still exist (a removed header
# must not stop nmake).
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][string]$ObjectDirectories  # comma-separated
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$lines = [Collections.Generic.List[string]]::new()
foreach ($directory in $ObjectDirectories.Split(",")) {
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) { continue }
    foreach ($json in Get-ChildItem -LiteralPath $directory -Filter '*.cpp.json') {
        $object = Join-Path $directory ($json.Name -replace '\.cpp\.json$', '.obj')
        $includes = (Get-Content -LiteralPath $json.FullName -Raw | ConvertFrom-Json).Data.Includes
        foreach ($header in $includes) {
            if ($header -notmatch 'program files|windows kits' -and [IO.File]::Exists($header)) {
                $lines.Add("${object}: `"$header`"")
            }
        }
    }
}
[IO.File]::WriteAllLines([IO.Path]::GetFullPath($Output), $lines)
