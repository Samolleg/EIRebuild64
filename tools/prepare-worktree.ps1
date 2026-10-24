param([string]$VSInstallDir)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$cache = Join-Path $env:USERPROFILE '.nuget/packages'
[xml]$config = Get-Content -LiteralPath (Join-Path $repo 'packages.config') -Raw

# Resolve every pinned package locally before writing any build dependencies.
# No NuGet restore command: it could silently fall back to a network source.
$packages = foreach ($package in $config.packages.package) {
    $id = [string]$package.id
    $version = [string]$package.version
    $source = Join-Path $cache ($id.ToLowerInvariant() + '/' + $version.ToLowerInvariant())
    $nuspec = Join-Path $source ($id.ToLowerInvariant() + '.nuspec')
    if (!(Test-Path -LiteralPath $nuspec -PathType Leaf)) {
        throw "Missing local NuGet package $id $version at $source. Nothing will be downloaded."
    }
    [xml]$metadata = Get-Content -LiteralPath $nuspec -Raw
    $cachedId = $metadata.SelectSingleNode('/*[local-name()="package"]/*[local-name()="metadata"]/*[local-name()="id"]')
    $cachedVersion = $metadata.SelectSingleNode('/*[local-name()="package"]/*[local-name()="metadata"]/*[local-name()="version"]')
    if (!$cachedId -or !$cachedVersion -or $cachedId.InnerText -ine $id -or $cachedVersion.InnerText -ne $version) {
        throw "NuGet cache identity mismatch: $nuspec"
    }
    [pscustomobject]@{ Id = $id; Version = $version; Source = $source }
}
$jsonSource = Join-Path $repo 'SDKs/JSON'
if (!(Test-Path -LiteralPath (Join-Path $jsonSource 'json.hpp') -PathType Leaf)) {
    throw "Missing repository JSON headers: $jsonSource"
}

# Only ignored packages/ and out/ receive generated/copied files.
foreach ($package in $packages) {
    $destination = Join-Path $repo ('packages/' + $package.Id + '.' + $package.Version)
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Get-ChildItem -LiteralPath $package.Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $destination -Recurse -Force
    }
    Write-Output "Prepared NuGet $($package.Id) $($package.Version) from local cache."
}
$jsonDestination = Join-Path $repo 'out/include/nlohmann'
New-Item -ItemType Directory -Force -Path $jsonDestination | Out-Null
Get-ChildItem -LiteralPath $jsonSource -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $jsonDestination -Recurse -Force
}
Write-Output 'Prepared out/include/nlohmann from SDKs/JSON.'

$luaOutput = Join-Path $repo 'out/x64/Release/luajit'
$luaMissing = !(Test-Path -LiteralPath (Join-Path $luaOutput 'luajit.lib') -PathType Leaf)
$headersMissing = @('lua.h', 'lua.hpp', 'lauxlib.h', 'lualib.h', 'luaconf.h', 'luajit.h') | Where-Object {
    !(Test-Path -LiteralPath (Join-Path $luaOutput "include/$_") -PathType Leaf)
}
if ($luaMissing -or $headersMissing) {
    $buildArguments = @{}
    if ($VSInstallDir) { $buildArguments.VSInstallDir = $VSInstallDir }
    if ($headersMissing) { $buildArguments.Force = $true }
    & (Join-Path $PSScriptRoot 'luajit/build.ps1') @buildArguments
} else {
    Write-Output 'LuaJIT library and headers already prepared.'
}
Write-Output 'Worktree prepared for Release x64 host build.'
