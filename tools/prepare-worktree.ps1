#requires -Version 7.6.10
#requires -PSEdition Core
param(
    [string]$VSInstallDir,
    [string]$FFmpegSource,
    [string]$DlssSdkSource = $env:EIREBUILD64_DLSS_SDK
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-io.ps1')
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectRoot = $repo
while (!(Test-Path -LiteralPath (Join-Path $projectRoot 'RULES.md') -PathType Leaf)) {
    $parent = Split-Path -Parent $projectRoot
    if (!$parent -or $parent -eq $projectRoot) { throw 'Project RULES.md not found above worktree.' }
    $projectRoot = $parent
}
if (!$FFmpegSource) { $FFmpegSource = Join-Path $projectRoot 'deps/ffmpeg/8.1.3/x64' }
& (Join-Path $PSScriptRoot 'physx/build.ps1') -ProjectRoot $projectRoot
$physxDestination = Join-Path $repo 'dependencies/physx/5.6.1'
Copy-BuildTree (Join-Path $projectRoot 'deps/physx/5.6.1/sdk') $physxDestination
if (!$DlssSdkSource) { $DlssSdkSource = Join-Path $projectRoot 'deps/dlss-sdk/v310.9.16' }
$dlssFiles = @('include/nvsdk_ngx.h', 'include/nvsdk_ngx_defs.h', 'include/nvsdk_ngx_params.h',
    'include/nvsdk_ngx_helpers.h', 'include/nvsdk_ngx_helpers_d3d.h', 'include/nvsdk_ngx_helpers_cuda.h',
    'lib/Windows_x86_64/x64/nvsdk_ngx_s.lib', 'lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib',
    'include/nvsdk_ngx_defs_dlssg.h', 'include/nvsdk_ngx_params_dlssg.h',
    'include/nvsdk_ngx_helpers_dlssg.h', 'include/nvsdk_ngx_helpers_dlssg_d3d.h',
    'include/nvsdk_ngx_defs_dlssd.h', 'include/nvsdk_ngx_params_dlssd.h',
    'include/nvsdk_ngx_helpers_dlssd.h', 'include/nvsdk_ngx_helpers_dlssd_d3d.h',
    'lib/Windows_x86_64/rel/nvngx_dlss.dll', 'lib/Windows_x86_64/rel/nvngx_dlssg.dll', 'lib/Windows_x86_64/rel/nvngx_dlssd.dll', 'LICENSE.txt')
foreach ($file in $dlssFiles) {
    if (!(Test-Path -LiteralPath (Join-Path $DlssSdkSource $file) -PathType Leaf)) {
        throw "Missing DLSS SDK v310.9.16 file: $DlssSdkSource/$file. Pass -DlssSdkSource or set EIREBUILD64_DLSS_SDK. Nothing will be downloaded."
    }
}
. (Join-Path $PSScriptRoot 'sdk-files.ps1')
# Resolve pinned local D3D12 inputs before copying any dependencies.
$d3d12Packages = @(
    @{ Source='deps/fidelityfx-sdk/2.3.0'; Destination='dependencies/fidelityfx/2.3.0'; Files=@(
        'api/include/*','framegeneration/include/*','upscalers/include/*',
        'signedbin/amd_fidelityfx_loader_dx12.lib','signedbin/amd_fidelityfx_loader_dx12.dll',
        'signedbin/amd_fidelityfx_framegeneration_dx12.dll','signedbin/amd_fidelityfx_upscaler_dx12.dll','license.md') },
    @{ Source='deps/rtxdi/3.2.0/RTXDI-Library-b67f61cd0bfa6792acbf5429bb29a68ee5cdd808'; Destination='dependencies/rtxdi/3.2.0'; Files=@(
        'Include/*','LICENSE.txt') },
    @{ Source='deps/agility-sdk/1.620.2'; Destination='dependencies/agility/1.620.2'; Files=@(
        'build/native/include/*','build/native/bin/x64/D3D12Core.dll','LICENSE.txt') },
    @{ Source='deps/nvapi/70d337db/nvapi-70d337db9186e968eab622f7e786de7e437faf3d'; Destination='dependencies/nvapi/70d337db'; Files=@(
        '*.h','amd64/nvapi64.lib','License.txt') },
    @{ Source='deps/dxc/1.9.2609'; Destination='dependencies/dxc/1.9.2609'; Files=@(
        'bin/x64/dxc.exe','bin/x64/dxcompiler.dll','bin/x64/dxil.dll',
        'LICENCE-MIT.txt','LICENSE-LLVM.txt','LICENSE-MS.txt') }
)
$d3d12Files = foreach ($package in $d3d12Packages) {
    $source = Join-Path $projectRoot $package.Source
    foreach ($pattern in $package.Files) {
        $matches = @(Get-LocalSdkFiles $source $pattern)
        if (!$matches.Count) { throw "Missing local D3D12 SDK input: $source/$pattern. Nothing will be downloaded." }
        foreach ($file in $matches) {
            [pscustomobject]@{ Source=$file.FullName; Destination=(Join-Path (Join-Path $repo $package.Destination) $file.FullName.Substring($source.Length+1)) }
        }
    }
}
# cgltf v1.15 is header-only and provided by the coordinator's local deps.
$cgltfSource = Join-Path $projectRoot 'deps/cgltf/v1.15'
foreach ($file in 'cgltf.h','LICENSE') {
    $source = Join-Path $cgltfSource $file
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing cgltf v1.15 input: $source" }
    $destination = Join-Path $repo ('dependencies/cgltf/v1.15/' + $file)
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    $null=Copy-BuildFile $source $destination
}
$cache = Join-Path $projectRoot 'deps/nuget'
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
foreach ($file in $d3d12Files) {
    New-Item -ItemType Directory -Force -Path (Split-Path $file.Destination) | Out-Null
    $null=Copy-BuildFile $file.Source $file.Destination
}
Write-Output 'Prepared FidelityFX 2.3.0, NVAPI 70d337db and DXC 1.9.2609 from local deps.'
$dlssDestination = Join-Path $repo 'dependencies/dlss/v310.9.16'
foreach ($file in $dlssFiles) {
    $destination = Join-Path $dlssDestination $file
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    $null=Copy-BuildFile (Join-Path $DlssSdkSource $file) $destination
}
Write-Output "Prepared DLSS SDK v310.9.16 x64 /MT and release runtime from $DlssSdkSource."
if ($ffmpegMissing) {
    New-Item -ItemType Directory -Force -Path (Split-Path $ffmpegDestination) | Out-Null
    Copy-Item -LiteralPath $FFmpegSource -Destination $ffmpegDestination -Recurse
    Write-Output "Prepared FFmpeg 8.1.3 x64 from $FFmpegSource."
}
foreach ($package in $packages) {
    $destination = Join-Path $repo ('packages/' + $package.Id + '.' + $package.Version)
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-BuildTree $package.Source $destination
    Write-Output "Prepared NuGet $($package.Id) $($package.Version) from local cache."
}
$jsonDestination = Join-Path $repo 'out/include/nlohmann'
New-Item -ItemType Directory -Force -Path $jsonDestination | Out-Null
Copy-BuildTree $jsonSource $jsonDestination
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

$zstdSource = Join-Path $projectRoot "deps/zstd/v1.5.7"
if (!(Test-Path (Join-Path $zstdSource "lib/zstd.h"))) { throw "Missing local zstd 1.5.7; nothing will be downloaded." }
$zstdDestination = Join-Path $repo "dependencies/zstd/v1.5.7"
New-Item -ItemType Directory -Force -Path $zstdDestination | Out-Null
Copy-BuildTree (Join-Path $zstdSource "lib") (Join-Path $zstdDestination "lib")
foreach($name in "LICENSE","COPYING"){$null=Copy-BuildFile (Join-Path $zstdSource $name) (Join-Path $zstdDestination $name)}
