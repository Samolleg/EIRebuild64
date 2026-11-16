# EID3D11_SHADER_STORE persists DXBC across worktrees in CacheRoot/ShaderStore.
# Compiler-keyed entries share the cache size limit (20 GB by default).
param(
    [string]$CacheRoot,
    [string]$RendererAssetsSource,
    [string]$RendererMaterialsSource,
    [ValidateRange(1,1024)][int]$CacheLimitGB = 20,
    [ValidateRange(1,100)][int]$CacheVersions = 2
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$task = Split-Path $repo
$project = $repo
while (!(Test-Path -LiteralPath (Join-Path $project 'RULES.md'))) {
    $parent = Split-Path $project
    if (!$parent -or $parent -eq $project) { throw 'Project RULES.md not found.' }
    $project = $parent
}
if (!$CacheRoot) { $CacheRoot = Join-Path $project 'builds/rebuild64/cache' }
if (!$RendererAssetsSource) { $RendererAssetsSource = Join-Path $project 'content/renderer-assets/Engine64' }
if (!$RendererMaterialsSource) { $RendererMaterialsSource = Join-Path $project 'content/renderer-materials/Engine64' }
if (!(Test-Path -LiteralPath (Join-Path $RendererAssetsSource 'eid3d_hosted/hosted_plants.bin'))) { throw 'Renderer 1126 hosted assets are missing from the selected content source.' }
if (!(Test-Path -LiteralPath (Join-Path $RendererMaterialsSource 'classes.json'))) { throw 'Renderer materials are missing from the selected content source.' }
$CacheRoot = [IO.Path]::GetFullPath($CacheRoot).TrimEnd('\','/')
if ($CacheRoot.StartsWith($task + '\',[StringComparison]::OrdinalIgnoreCase) -or
    $CacheRoot -eq $project -or $CacheRoot -eq $repo) { throw 'Cache must be outside task worktrees.' }
if ((Split-Path $repo -Leaf) -ne 'wt' -or !(Test-Path (Join-Path $task 'BRIEF.md'))) { throw 'Run from a task worktree.' }
$clock = [Diagnostics.Stopwatch]::StartNew()
$cores = 4
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'MSVC x64 tools not found.' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$vcversion = (Get-Content (Join-Path $vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
$vc = Join-Path $vs "VC/Tools/MSVC/$vcversion"
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkversion = (Get-ChildItem (Join-Path $sdk 'Include') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um/Windows.h') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
if (!$sdkversion) { throw 'Windows SDK not found.' }
$dumpbin = Join-Path $vc 'bin/Hostx64/x64/dumpbin.exe'
function Hash-File([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Manifest([string]$directory) {
    @(Get-ChildItem -LiteralPath $directory -File -Recurse | Where-Object { $_.Name -ne 'SHA256SUMS' } | Sort-Object FullName | ForEach-Object {
        (Hash-File $_.FullName) + '  ' + $_.FullName.Substring($directory.Length+1).Replace('\','/')
    })
}
function Write-Utf8($path,$lines) { [IO.File]::WriteAllLines($path,[string[]]$lines,(New-Object Text.UTF8Encoding($false))) }
function Remove-CacheEntry([string]$path) {
    $full = [IO.Path]::GetFullPath($path)
    if (!$full.StartsWith($CacheRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe cache removal: $full" }
    if (Get-ChildItem -LiteralPath $full -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw "Cache contains a link: $full" }
    if ((Get-Item -LiteralPath $full).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Cache entry is a link: $full" }
    Remove-Item -LiteralPath $full -Recurse -Force
}
function Sync-Tree([string]$source,[string]$destination,[string[]]$files) {
    # Incremental builds need one stable source path. Copy only files whose bytes
    # differ and stamp them now, so MSBuild rebuilds exactly what changed.
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $listPath = Join-Path $destination '.ei-sources'
    $previous = @(); if (Test-Path -LiteralPath $listPath) { $previous = @(Get-Content -LiteralPath $listPath) }
    $now = [datetime]::UtcNow; $copied = 0
    foreach ($relative in $files) {
        $from = Join-Path $source $relative; $to = Join-Path $destination $relative
        if (!(Test-Path -LiteralPath $from -PathType Leaf)) { continue }
        $same = (Test-Path -LiteralPath $to -PathType Leaf) -and ((Get-Item -LiteralPath $from).Length -eq (Get-Item -LiteralPath $to).Length) -and ((Hash-File $from) -eq (Hash-File $to))
        if (!$same) {
            New-Item -ItemType Directory -Force -Path (Split-Path $to) | Out-Null
            Copy-Item -LiteralPath $from -Destination $to -Force
            (Get-Item -LiteralPath $to).LastWriteTimeUtc = $now
            $copied++
        }
    }
    $current = [Collections.Generic.HashSet[string]]::new([string[]]$files,[StringComparer]::OrdinalIgnoreCase)
    $removed = 0
    foreach ($relative in $previous) {
        if (!$current.Contains($relative)) { $gone = Join-Path $destination $relative; if (Test-Path -LiteralPath $gone -PathType Leaf) { Remove-Item -LiteralPath $gone -Force; $removed++ } }
    }
    Write-Utf8 $listPath $files
    Write-Host "Persistent build tree: $copied changed, $removed removed, $($files.Count) sources."
}
function Trim-Cache {
    foreach ($group in Get-ChildItem -LiteralPath $CacheRoot -Directory) {
        if ($group.Name -eq 'work') { continue }
        if ($group.Name -notin @('LuaJIT','DirectXTex','DirectXTK','RendererShaders','Recast','ShaderStore')) { throw "Unknown cache directory: $($group.FullName)" }
        $entries = @(Get-ChildItem -LiteralPath $group.FullName -Directory | Sort-Object LastWriteTimeUtc -Descending)
        foreach ($entry in $entries) { if ($entry.Name -notmatch '^[0-9a-f]{64}$') { throw "Unknown cache entry: $($entry.FullName)" } }
        $entries | Select-Object -Skip $CacheVersions | ForEach-Object { Remove-CacheEntry $_.FullName }
    }
    $entries = @(Get-ChildItem -LiteralPath $CacheRoot -Directory | Where-Object Name -ne 'work' | Get-ChildItem -Directory | Sort-Object LastWriteTimeUtc)
    [long]$size = (Get-ChildItem -LiteralPath $CacheRoot -File -Recurse | Measure-Object Length -Sum).Sum
    foreach ($entry in $entries) {
        if ($size -le $CacheLimitGB * 1GB) { break }
        $bytes = (Get-ChildItem -LiteralPath $entry.FullName -File -Recurse | Measure-Object Length -Sum).Sum
        Remove-CacheEntry $entry.FullName
        $size -= $bytes
    }
}
$saved = @{}
foreach ($name in 'TMP','TEMP','_MSPDBSRV_ENDPOINT_','CL','_CL_','LINK','_LINK_','EID3D11_SHADER_STORE','MSBUILDDISABLENODEREUSE') { $saved[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
$lock = $null
Push-Location $repo
try {
    $privateTemp = Join-Path $task ('tmp/build-' + [guid]::NewGuid().ToString('N'))
    $ancestor = $CacheRoot
    while ($ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Cache ancestor is a link: $ancestor" }
        $ancestor = Split-Path $ancestor
    }
    if (Test-Path -LiteralPath (Join-Path $task 'kit')) { throw 'Kit already exists; preserve it before an authorized new build.' }
    New-Item -ItemType Directory -Force -Path $privateTemp,$CacheRoot | Out-Null
    # Hold a filesystem lock through restore, publication and pruning. No shared entry
    # can be removed or partially restored by another task using this script.
    $lock = [IO.File]::Open((Join-Path $CacheRoot '.build.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    foreach ($item in Get-ChildItem -LiteralPath $CacheRoot -Recurse -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Cache link rejected: $($item.FullName)" }
    }
    if ((Get-Item -LiteralPath $CacheRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Cache root is a link.' }
    Trim-Cache
    $env:MSBUILDDISABLENODEREUSE = '1'
    $env:TMP = $privateTemp; $env:TEMP = $privateTemp
    $env:_MSPDBSRV_ENDPOINT_ = 'EI-' + [guid]::NewGuid().ToString('N')
    foreach ($name in 'CL','_CL_','LINK','_LINK_') { [Environment]::SetEnvironmentVariable($name,$null,'Process') }
    $shaderCompilerKey = Hash-File "$env:SystemRoot/System32/d3dcompiler_47.dll"
    $env:EID3D11_SHADER_STORE = Join-Path $CacheRoot ("ShaderStore/" + $shaderCompilerKey)
    New-Item -ItemType Directory -Force -Path $env:EID3D11_SHADER_STORE | Out-Null
    (Get-Item -LiteralPath $env:EID3D11_SHADER_STORE).LastWriteTimeUtc = [datetime]::UtcNow
    # Shared build metadata and toolchain; source payloads are keyed per library.
    $inventory = [Collections.Generic.List[string]]::new()
    $tracked = @(& git -C $repo -c core.quotepath=false ls-files --cached --others --exclude-standard | Sort-Object -Unique)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate sources.' }
    # Build in one persistent tree so unchanged objects, PCH and incremental LTCG
    # survive between tasks; the task worktree keeps only sources and the kit.
    $sourceRepo = $repo
    $repo = Join-Path $CacheRoot 'work'
    Sync-Tree $sourceRepo $repo $tracked
    Set-Location $repo
    foreach ($relative in $tracked | Where-Object { $_ -match '\.(vcxproj|props|targets|ps1|cs)$' }) { $inventory.Add($relative + '=' + (Hash-File (Join-Path $repo $relative))) }
    foreach ($directory in @((Join-Path $vc 'include'),(Join-Path $vc 'atlmfc/include'),(Join-Path $sdk "Include/$sdkversion"))) {
        foreach ($file in Get-ChildItem -LiteralPath $directory -File -Recurse | Sort-Object FullName) { $inventory.Add($file.FullName + '=' + (Hash-File $file.FullName)) }
    }
    foreach ($file in @($msbuild,(Join-Path $vc 'bin/Hostx64/x64/cl.exe'),(Join-Path $vc 'bin/Hostx64/x64/c1xx.dll'),(Join-Path $vc 'bin/Hostx64/x64/c2.dll'),(Join-Path $vc 'bin/Hostx64/x64/link.exe'),(Join-Path $vc 'bin/Hostx64/x64/lib.exe'),(Join-Path $vc 'bin/Hostx64/x64/mspdbsrv.exe'),(Join-Path $sdk "bin/$sdkversion/x64/rc.exe"),(Join-Path $sdk "bin/$sdkversion/x64/fxc.exe"),"$env:SystemRoot/System32/d3dcompiler_47.dll")) { $inventory.Add($file + '=' + (Hash-File $file)) }
    $inventory.Add("Release|x64|v143|$vcversion|$sdkversion|MT|MP|FS")
    $keyBytes = [Text.Encoding]::UTF8.GetBytes(($inventory -join "`n"))
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $key = ([BitConverter]::ToString($sha.ComputeHash($keyBytes))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    $groups = @(
        @{ Name='LuaJIT'; Sources='^third_party/luajit/'; Patterns=@('out/x64/Release/luajit/*') },
        @{ Name='DirectXTex'; Sources='^external/DirectXTex/'; Patterns=@('out/x64/Release/DirectXTex/*.lib','out/x64/Release/DirectXTex/*.pdb') },
        @{ Name='DirectXTK'; Sources='^external/DirectXTK/'; Patterns=@('out/x64/Release/DirectXTK/*.lib','out/x64/Release/DirectXTK/*.pdb') },
        @{ Name='RendererShaders'; Sources='^(external/owned-renderer/|OwnedRenderer/|Game/OwnedRender/)'; Patterns=@('out/x64/Release/RendererShaders/eid3d11_*') },
        @{ Name='Recast'; Sources='^external/recastnavigation/'; Patterns=@('out/obj/x64/Release/EI_Rebuild/Recast*.obj','out/obj/x64/Release/EI_Rebuild/Detour*.obj','out/obj/x64/Release/EI_Rebuild/Recast.compiler.pdb') }
    )
    # The persistent tree already keeps every library output; per-library payload
    # restores would restamp files and force relinks, so they are not used here.
    $groups = @()
    $sourceCommit = (& git -C $sourceRepo -c "safe.directory=$sourceRepo" describe --always --abbrev=12 --dirty --exclude=*).Trim()
    if ($LASTEXITCODE -ne 0 -or !$sourceCommit) { throw 'Cannot identify source commit.' }
    $arguments = @("/p:ScriptCheckCommit=$sourceCommit",'EI_Rebuild.sln',"/m:$cores",'/nr:false','/t:Build','/p:Configuration=Release','/p:Platform=x64','/p:PreferredToolArchitecture=x64',"/p:VCToolsVersion=$vcversion","/p:WindowsTargetPlatformVersion=$sdkversion","/p:UserRootDir=$privateTemp\",'/nologo','/v:minimal')
    foreach ($group in $groups) {
        $sourceInventory = @($key)
        foreach ($relative in $tracked | Where-Object { $_ -match $group.Sources }) { $sourceInventory += ($relative + '=' + (Hash-File (Join-Path $repo $relative))) }
        $sourceSha = [Security.Cryptography.SHA256]::Create()
        try { $group.Key = ([BitConverter]::ToString($sourceSha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($sourceInventory -join "`n"))))).Replace('-','').ToLowerInvariant() } finally { $sourceSha.Dispose() }
        $entry = Join-Path $CacheRoot ($group.Name + '/' + $group.Key)
        $group.Entry = $entry; $group.Hit = $false
        $manifest = Join-Path $entry 'SHA256SUMS'
        if (Test-Path -LiteralPath $manifest) {
            $expected = @(Get-Content -LiteralPath $manifest)
            $actual = @(Manifest $entry)
            if ($expected.Count -gt 0 -and (($expected -join "`n") -ceq ($actual -join "`n"))) {
                foreach ($file in Get-ChildItem -LiteralPath $entry -File -Recurse | Where-Object Name -ne 'SHA256SUMS') {
                    $destination = Join-Path $repo $file.FullName.Substring($entry.Length+1)
                    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
                    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
                    (Get-Item -LiteralPath $destination).LastWriteTimeUtc = [datetime]::UtcNow
                }
                $group.Hit = $true
                (Get-Item -LiteralPath $entry).LastWriteTimeUtc = [datetime]::UtcNow
                if ($group.Name -eq 'Recast') { $arguments += '/p:EIRecastCacheHit=true' }
                elseif ($group.Name -ne 'LuaJIT') { $arguments += ('/p:EICache' + $group.Name + '=true') }
            } else { Remove-CacheEntry $entry }
        }
        Write-Host "$($group.Name): cache hit=$($group.Hit) key=$($group.Key)"
    }
    & (Join-Path $repo 'tools/prepare-worktree.ps1') -VSInstallDir $vs
    if (!$?) { throw 'Preparation failed.' }
    Write-Host "MSBuild workers: $cores; compiler /MP; isolated endpoint $env:_MSPDBSRV_ENDPOINT_"
    Add-Type -Path (Join-Path $PSScriptRoot 'BuildProcess.cs')
    $arguments += "/flp:logfile=$task/build.log;verbosity=normal"
    # Quote Windows argv, doubling trailing backslashes before the closing quote.
    $commandLine = ($arguments | ForEach-Object {
        '"' + ([regex]::Replace(([regex]::Replace($_, '(\\*)"', '$1$1\"')), '(\\+)$', '$1$1')) + '"'
    }) -join ' '
    $buildExit = [EIBuildProcess]::Run($msbuild, $commandLine, $repo)
    if ($buildExit -ne 0) { throw "MSBuild failed ($buildExit); no serial retry." }
    foreach ($name in 'DirectXTex','DirectXTK') {
        $compilerPdb = Join-Path $repo "out/obj/x64/Release/$name/$name.compiler.pdb"
        if (Test-Path -LiteralPath $compilerPdb) { Copy-Item -LiteralPath $compilerPdb -Destination (Join-Path $repo "out/x64/Release/$name/") -Force }
    }
    foreach ($group in $groups | Where-Object { !$_.Hit }) {
        $entry = $group.Entry
        if (Test-Path -LiteralPath $entry) { Remove-CacheEntry $entry }
        New-Item -ItemType Directory -Force -Path $entry | Out-Null
        foreach ($pattern in $group.Patterns) {
            $files = @(Get-ChildItem -Path (Join-Path $repo $pattern) -File -Recurse)
            foreach ($file in $files) {
                $destination = Join-Path $entry $file.FullName.Substring($repo.Length+1)
                New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
                Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
            }
        }
        $lines = @(Manifest $entry)
        if (!$lines.Count) { throw "Empty cache payload: $($group.Name)" }
        Write-Utf8 (Join-Path $entry 'SHA256SUMS') $lines
    }
    Trim-Cache
    $kit = Join-Path $task 'kit'
    if (Test-Path -LiteralPath $kit) { throw "Kit already exists: $kit; retain evidence and use a fresh task for timing." }
    New-Item -ItemType Directory -Path $kit | Out-Null
    foreach ($target in @(@('EI_Rebuild','EIRebuild64'),@('EIRebuild64NavBake','EIRebuild64NavBake'),@('EIRebuild64Pack','EIRebuild64Pack'),@('EIRebuild64ModelCheck','EIRebuild64ModelCheck'))) {
        Copy-Item -LiteralPath (Join-Path $repo "out/x64/Release/$($target[0])/$($target[1]).exe") -Destination $kit
        Copy-Item -LiteralPath (Join-Path $repo "out/x64/Release/$($target[0])/$($target[0]).pdb") -Destination (Join-Path $kit ($target[1] + '.pdb'))
    }
    foreach ($name in 'avcodec-62.dll','avformat-62.dll','avutil-60.dll','swscale-9.dll','swresample-6.dll') { Copy-Item -LiteralPath (Join-Path $repo ('dependencies/ffmpeg/8.1.3/x64/bin/' + $name)) -Destination $kit }
    foreach ($name in 'nvngx_dlss.dll','nvngx_dlssg.dll','nvngx_dlssd.dll') { Copy-Item -LiteralPath (Join-Path $repo ('dependencies/dlss/v310.9.16/lib/Windows_x86_64/rel/' + $name)) -Destination $kit }
    foreach ($name in 'amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_framegeneration_dx12.dll','amd_fidelityfx_upscaler_dx12.dll') { Copy-Item -LiteralPath (Join-Path $repo ('dependencies/fidelityfx/2.3.0/signedbin/' + $name)) -Destination $kit }
    New-Item -ItemType Directory -Path (Join-Path $kit 'D3D12') | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo 'dependencies/agility/1.620.2/build/native/bin/x64/D3D12Core.dll') -Destination (Join-Path $kit 'D3D12')
    Copy-Item -LiteralPath (Join-Path $repo 'out/x64/Release/EI_Rebuild/licenses') -Destination (Join-Path $kit 'licenses') -Recurse
    Copy-Item -LiteralPath $RendererMaterialsSource -Destination (Join-Path $kit 'eid3d_materials') -Recurse
    Copy-Item -LiteralPath (Join-Path $RendererAssetsSource 'eid3d_hosted') -Destination (Join-Path $kit 'eid3d_hosted') -Recurse
    Copy-Item -LiteralPath (Join-Path $repo 'external/owned-renderer/Host/d3d12/eid3d11_d3d12_pipelines.shipped.list') -Destination $kit
    Copy-Item -LiteralPath (Join-Path $repo 'external/owned-renderer/LICENSE.md') -Destination (Join-Path $kit 'licenses/EID3D11-LICENSE.md')
    Copy-Item -LiteralPath (Join-Path $repo 'external/owned-renderer/NOTICE.md') -Destination (Join-Path $kit 'licenses/EID3D11-NOTICE.md')
    foreach ($binary in Get-ChildItem $kit -File -Recurse | Where-Object Extension -in '.exe','.dll') {
        $imports = & $dumpbin /nologo /dependents $binary.FullName
        if ($LASTEXITCODE -ne 0) { throw "dumpbin failed: $($binary.Name)" }
        $imports | Out-File (Join-Path $task ($binary.Name + '.dependents.txt')) -Encoding utf8
        if ($binary.Name -match '^(nvngx_|amd_fidelityfx_).*\.dll$' -or $binary.Name -eq 'D3D12Core.dll') {
            $signature = Get-AuthenticodeSignature -LiteralPath $binary.FullName
            if ($signature.Status -ne 'Valid') { throw "Invalid vendor signature: $($binary.Name): $($signature.Status)" }
        } elseif ($imports -match '(?i)(MSVCP\d+|VCRUNTIME\d+|mfc\d+|ucrtbase|api-ms-win-crt-).*\.dll') {
            throw "Dynamic VC runtime: $($binary.Name)"
        }
        if ($binary.Name -ieq 'EIRebuild64.exe') {
            # dumpbin lists delay-load dependencies separately from startup imports.
            $staticImports = (($imports -join "`n") -split '(?im)^\s*Image has the following delay load dependencies:')[0]
            if ($staticImports -match '(?im)^\s*(nvngx_|amd_fidelityfx_)[^\s]*\.dll\s*$') {
                throw "Static vendor DLL import: $($binary.Name): $($Matches[0].Trim())"
            }
        }
    }
    Write-Utf8 (Join-Path $kit 'SHA256SUMS') (Manifest $kit)
    $clock.Stop()
    $bytes = (Get-ChildItem -LiteralPath $CacheRoot -File -Recurse | Measure-Object Length -Sum).Sum
    Write-Utf8 (Join-Path $task 'build-result.txt') @("Seconds=$($clock.Elapsed.TotalSeconds)","MSBuildWorkers=$cores","CacheBytes=$bytes","CacheKey=$key","CacheRoot=$CacheRoot",("Hits=" + (($groups | Where-Object Hit | ForEach-Object Name) -join ',')))
    Write-Host "Built kit in $($clock.Elapsed.TotalSeconds) seconds; cache $bytes bytes."
} catch {
    $clock.Stop()
    $bytes = 0
    if (Test-Path -LiteralPath $CacheRoot) { $bytes = (Get-ChildItem -LiteralPath $CacheRoot -File -Recurse | Measure-Object Length -Sum).Sum }
    Write-Utf8 (Join-Path $task 'build-failure.txt') @("Seconds=$($clock.Elapsed.TotalSeconds)","MSBuildWorkers=$cores","CacheBytes=$bytes",$_.Exception.Message)
    throw
} finally {
    if ($lock) { $lock.Dispose() }
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name,$saved[$name],'Process') }
    Pop-Location
}
