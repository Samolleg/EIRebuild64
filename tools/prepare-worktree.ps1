param(
    [string]$VSInstallDir,
    [string]$FFmpegSource,
    [string]$DlssSdkSource = $env:EIREBUILD64_DLSS_SDK
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$FFmpegSource) {
    $work = Split-Path $repo
    if ((Split-Path $work -Leaf) -eq 'eirebuild64-wt') { $work = Split-Path $work }
    $FFmpegSource = Join-Path $work 'deps64/ffmpeg/8.1.3/x64'
}
if (!$DlssSdkSource) {
    $work = Split-Path $repo
    if ((Split-Path $work -Leaf) -eq 'eirebuild64-wt') { $work = Split-Path $work }
    $DlssSdkSource = Join-Path $work 'deps64/dlss-sdk/v310.9.16'
}
$dlssFiles = @('include/nvsdk_ngx.h', 'include/nvsdk_ngx_defs.h', 'include/nvsdk_ngx_params.h',
    'include/nvsdk_ngx_helpers.h', 'include/nvsdk_ngx_helpers_d3d.h', 'include/nvsdk_ngx_helpers_cuda.h',
    'lib/Windows_x86_64/x64/nvsdk_ngx_d.lib',
    'lib/Windows_x86_64/rel/nvngx_dlss.dll', 'LICENSE.txt')
foreach ($file in $dlssFiles) {
    if (!(Test-Path -LiteralPath (Join-Path $DlssSdkSource $file) -PathType Leaf)) {
        throw "Missing DLSS SDK v310.9.16 file: $DlssSdkSource/$file. Pass -DlssSdkSource or set EIREBUILD64_DLSS_SDK. Nothing will be downloaded."
    }
}
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

$ffmpegDestination = Join-Path $repo 'dependencies/ffmpeg/8.1.3/x64'
$ffmpegMissing = !(Test-Path -LiteralPath $ffmpegDestination)
if ($ffmpegMissing -and !(Test-Path -LiteralPath $FFmpegSource -PathType Container)) {
    throw "Missing FFmpeg 8.1.3 x64 dependency at $FFmpegSource. Build it using tools/ffmpeg/README.md or pass -FFmpegSource. Nothing will be downloaded."
}

# Only ignored packages/, out/ and dependencies/ receive generated/copied files.
$dlssDestination = Join-Path $repo 'dependencies/dlss/v310.9.16'
foreach ($file in $dlssFiles) {
    $destination = Join-Path $dlssDestination $file
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $DlssSdkSource $file) -Destination $destination -Force
}
Write-Output "Prepared DLSS SDK v310.9.16 x64 /MD and release runtime from $DlssSdkSource."
if ($ffmpegMissing) {
    New-Item -ItemType Directory -Force -Path (Split-Path $ffmpegDestination) | Out-Null
    Copy-Item -LiteralPath $FFmpegSource -Destination $ffmpegDestination -Recurse
    Write-Output "Prepared FFmpeg 8.1.3 x64 from $FFmpegSource."
}
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
