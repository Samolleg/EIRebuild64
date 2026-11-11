param(
    [string]$VSInstallDir,
    [string]$FFmpegSource,
    [string]$DlssSdkSource = $env:EIREBUILD64_DLSS_SDK
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectRoot = $repo
while (!(Test-Path -LiteralPath (Join-Path $projectRoot 'RULES.md') -PathType Leaf)) {
    $parent = Split-Path -Parent $projectRoot
    if (!$parent -or $parent -eq $projectRoot) { throw 'Project RULES.md not found above worktree.' }
    $projectRoot = $parent
}
if (!$FFmpegSource) { $FFmpegSource = Join-Path $projectRoot 'deps/ffmpeg/8.1.3/x64' }
if (!$DlssSdkSource) { $DlssSdkSource = Join-Path $projectRoot 'deps/dlss-sdk/v310.9.16' }
$dlssFiles = @('include/nvsdk_ngx.h', 'include/nvsdk_ngx_defs.h', 'include/nvsdk_ngx_params.h',
    'include/nvsdk_ngx_helpers.h', 'include/nvsdk_ngx_helpers_d3d.h', 'include/nvsdk_ngx_helpers_cuda.h',
    'lib/Windows_x86_64/x64/nvsdk_ngx_s.lib', 'lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib',
    'include/nvsdk_ngx_defs_dlssg.h', 'include/nvsdk_ngx_params_dlssg.h',
    'include/nvsdk_ngx_helpers_dlssg.h', 'include/nvsdk_ngx_helpers_dlssg_d3d.h',
    'lib/Windows_x86_64/rel/nvngx_dlss.dll', 'lib/Windows_x86_64/rel/nvngx_dlssg.dll', 'LICENSE.txt')
foreach ($file in $dlssFiles) {
    if (!(Test-Path -LiteralPath (Join-Path $DlssSdkSource $file) -PathType Leaf)) {
        throw "Missing DLSS SDK v310.9.16 file: $DlssSdkSource/$file. Pass -DlssSdkSource or set EIREBUILD64_DLSS_SDK. Nothing will be downloaded."
    }
}
# Resolve pinned local D3D12 inputs before copying any dependencies.
$d3d12Packages = @(
    @{ Source='deps/fidelityfx-sdk/2.3.0'; Destination='dependencies/fidelityfx/2.3.0'; Files=@(
        'api/include/*','framegeneration/include/*','upscalers/include/*',
        'signedbin/amd_fidelityfx_loader_dx12.lib','signedbin/amd_fidelityfx_loader_dx12.dll',
        'signedbin/amd_fidelityfx_framegeneration_dx12.dll','license.md') },
    @{ Source='deps/nvapi/70d337db/nvapi-70d337db9186e968eab622f7e786de7e437faf3d'; Destination='dependencies/nvapi/70d337db'; Files=@(
        '*.h','amd64/nvapi64.lib','License.txt') },
    @{ Source='deps/dxc/1.9.2609'; Destination='dependencies/dxc/1.9.2609'; Files=@(
        'bin/x64/dxc.exe','bin/x64/dxcompiler.dll','bin/x64/dxil.dll',
        'LICENCE-MIT.txt','LICENSE-LLVM.txt','LICENSE-MS.txt') }
)
$d3d12Files = foreach ($package in $d3d12Packages) {
    $source = Join-Path $projectRoot $package.Source
    foreach ($pattern in $package.Files) {
        $matches = @(Get-ChildItem -Path (Join-Path $source $pattern) -File -Recurse -ErrorAction Stop)
        if (!$matches.Count) { throw "Missing local D3D12 SDK input: $source/$pattern. Nothing will be downloaded." }
        foreach ($file in $matches) {
            [pscustomobject]@{ Source=$file.FullName; Destination=(Join-Path (Join-Path $repo $package.Destination) $file.FullName.Substring($source.Length+1)) }
        }
    }
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
    Copy-Item -LiteralPath $file.Source -Destination $file.Destination -Force
}
Write-Output 'Prepared FidelityFX 2.3.0, NVAPI 70d337db and DXC 1.9.2609 from local deps.'
$dlssDestination = Join-Path $repo 'dependencies/dlss/v310.9.16'
foreach ($file in $dlssFiles) {
    $destination = Join-Path $dlssDestination $file
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $DlssSdkSource $file) -Destination $destination -Force
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
