#requires -Version 7.6.10
#requires -PSEdition Core
# EID3D11_SHADER_STORE persists DXBC across worktrees in CacheRoot/ShaderStore.
# Compiler-keyed entries share the cache size limit (40 GiB by default).
param(
    [string]$CacheRoot = $env:EIREBUILD64_BUILD_CACHE,
    [switch]$PhysicsFxCheck,
    [string]$ShaderResourceManifest = $env:EIREBUILD64_SHADER_RESOURCE_MANIFEST,
    [string]$RendererAssetsSource,
    [string]$RendererMaterialsSource,
    [ValidateRange(1,1024)][int]$CacheLimitGB = 40,
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
if ($ShaderResourceManifest) { $ShaderResourceManifest = [IO.Path]::GetFullPath($ShaderResourceManifest) }
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
. (Join-Path $PSScriptRoot 'build-io.ps1')
Assert-BuildCacheRoot $CacheRoot (Join-Path $project 'builds/rebuild64/cache')
function Assert-ShaderProjectReuse([string]$Before,[string]$After) {
    [xml]$left=$Before;[xml]$right=$After
    foreach($document in @($left,$right)){
        foreach($node in @($document.SelectNodes('//*[local-name()="Target"]|//comment()'))){
            $null=$node.ParentNode.RemoveChild($node)
        }
    }
    if($left.OuterXml -cne $right.OuterXml){throw 'Shader compiler project inputs/properties changed; accepted resources cannot be reused.'}
}
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
function Trim-Cache {
    $files=@(Get-BuildFiles $CacheRoot)
    [long]$size=0; $sizes=@{}
    foreach($file in $files){
        $size+=$file.Length
        $relative=$file.FullName.Substring($CacheRoot.Length+1).Split('\')
        if($relative.Count -ge 3 -and $relative[0] -ne 'work'){
            $entry=Join-Path $CacheRoot ($relative[0]+'\'+$relative[1])
            if(!$sizes.ContainsKey($entry)){$sizes[$entry]=[long]0};$sizes[$entry]+=$file.Length
        }
    }
    $entries=@()
    foreach($group in [IO.DirectoryInfo]::new($CacheRoot).EnumerateDirectories()){
        if($group.Name -eq 'work'){continue}
        if($group.Name -notin @('LuaJIT','DirectXTex','DirectXTK','RendererShaders','Recast','ShaderStore')){throw "Unknown cache directory: $($group.FullName)"}
        $versions=@($group.EnumerateDirectories() | Sort-Object LastWriteTimeUtc -Descending)
        foreach($entry in $versions){if($entry.Name -notmatch '^[0-9a-f]{64}$'){throw "Unknown cache entry: $($entry.FullName)"}}
        for($i=0;$i -lt $versions.Count;$i++){
            $entry=$versions[$i]
            if($i -ge $CacheVersions){Remove-CacheEntry $entry.FullName;$size-=$sizes[$entry.FullName]}
            else{$entries+=$entry}
        }
    }
    foreach($entry in ($entries | Sort-Object LastWriteTimeUtc)){
        if($size -le $CacheLimitGB*1GB){break}
        Remove-CacheEntry $entry.FullName;$size-=$sizes[$entry.FullName]
    }
    if($size -gt $CacheLimitGB*1GB){
        throw "Persistent work exceeds cache limit ($size bytes); relocate nested/independent builds or increase CacheLimitGB. Work is never deleted by pruning."
    }
    return $size
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
    $lock = Enter-BuildLock (Join-Path $CacheRoot '.build.lock')
    $null = Trim-Cache
    $env:MSBUILDDISABLENODEREUSE = '1'
    $env:TMP = $privateTemp; $env:TEMP = $privateTemp
    $env:_MSPDBSRV_ENDPOINT_ = 'EI-' + [guid]::NewGuid().ToString('N')
    foreach ($name in 'CL','_CL_','LINK','_LINK_') { [Environment]::SetEnvironmentVariable($name,$null,'Process') }
    $shaderCompilerKey = Hash-File "$env:SystemRoot/System32/d3dcompiler_47.dll"
    $env:EID3D11_SHADER_STORE = Join-Path $CacheRoot ("ShaderStore/" + $shaderCompilerKey)
    New-Item -ItemType Directory -Force -Path $env:EID3D11_SHADER_STORE | Out-Null
    (Get-Item -LiteralPath $env:EID3D11_SHADER_STORE).LastWriteTimeUtc = [datetime]::UtcNow
    # Shared build metadata and toolchain; source payloads are keyed per library.
    $tracked = @(& git -C $repo -c core.quotepath=false ls-files --cached --others --exclude-standard | Sort-Object -Unique)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate sources.' }
    # Build in one persistent tree so unchanged objects, PCH and incremental LTCG
    # survive between tasks; the task worktree keeps only sources and the kit.
    $sourceRepo = $repo
    $repo = Join-Path $CacheRoot 'work'
    $shaderReuse = $null
    if ($ShaderResourceManifest) {
        # Only a clean task, an accepted current EXE and a pinned change list may
        # borrow the shader bytes. Refuse changes to shader inputs, stale cache
        # libraries, unknown payloads or a moving main instead of recompiling.
        $shaderReuse = Get-Content -LiteralPath $ShaderResourceManifest -Raw | ConvertFrom-Json
        if ($shaderReuse.version -ne 1 -or $shaderReuse.base_commit -notmatch '^[0-9a-f]{40}$') { throw 'Invalid shader reuse manifest.' }
        $accepted = [IO.Path]::GetFullPath([string]$shaderReuse.accepted_exe)
        $acceptedPaths = @('current','previous' | ForEach-Object {
            [IO.Path]::GetFullPath((Join-Path $project "builds/rebuild64/$_/EIRebuild64.exe"))
        })
        if ($acceptedPaths -notcontains $accepted -or
            (Hash-File $accepted) -cne [string]$shaderReuse.accepted_exe_sha256) { throw 'Accepted shader source EXE changed.' }
        $base = [string]$shaderReuse.base_commit
        $main = (& git -C $sourceRepo rev-parse main).Trim()
        if ($LASTEXITCODE -ne 0) { throw 'Cannot determine current main.' }
        $head = (& git -C $sourceRepo rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0) { throw 'Cannot determine build HEAD.' }
        & git -C $sourceRepo merge-base --is-ancestor $main HEAD
        if ($LASTEXITCODE -ne 0) {
            throw 'Shader reuse requires all current main changes in the build checkout.'
        }
        $receipt = @(Get-Content -LiteralPath (Join-Path $project 'docs/tasks.log') | Where-Object {
            $v = $_ -split "`t"; $v.Count -ge 7 -and $v[3] -ceq $base -and $v[5] -ceq [string]$shaderReuse.accepted_exe_sha256 -and $v[6] -eq 'accepted'
        })
        if (!$receipt.Count) { throw 'Accepted EXE and base commit lack a task receipt.' }
        & git -C $sourceRepo merge-base --is-ancestor $base HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Shader reuse base is not an ancestor.' }
        $status = @(& git -C $sourceRepo status --porcelain --untracked-files=all)
        if ($LASTEXITCODE -ne 0 -or $status.Count) { throw 'Shader reuse requires a clean task worktree.' }
        $actual = @(& git -C $sourceRepo -c core.quotepath=false diff --name-only "$base...HEAD" | Sort-Object)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot list shader reuse changes.' }
        $expected = @($shaderReuse.changes | ForEach-Object { [string]$_.path } | Sort-Object)
        if (($actual -join "`n") -cne ($expected -join "`n")) { throw 'Shader reuse changed-file list differs.' }
        $deleted = @(& git -C $sourceRepo diff --name-only --diff-filter=D "$base...HEAD")
        if ($LASTEXITCODE -ne 0) { throw 'Cannot verify deleted shader-reuse inputs.' }
        foreach ($change in $shaderReuse.changes) {
            $relative = [string]$change.path
            # Packaging orchestration is not a shader producer. For its project,
            # require every compile/link input and property to match the accepted base.
            if ($relative -ceq 'OwnedRenderer/RendererShaders.vcxproj') {
                $oldProject=(& git -C $sourceRepo show "${base}:$relative") -join "`n"
                if($LASTEXITCODE -ne 0){throw 'Accepted shader project is unavailable.'}
                Assert-ShaderProjectReuse $oldProject ([IO.File]::ReadAllText((Join-Path $sourceRepo $relative)))
            } elseif ($relative -cnotin @('tools/shader-build.ps1','tools/test-shader-build.ps1') -and
                $relative -match '(?i)(^|/)(shader[^/]*|[^/]*\.(hlsl|fx|fxh|dxil|dxbc))$') { throw "Shader input changed: $relative" }
            $removed = $change.PSObject.Properties.Name -contains 'deleted' -and [bool]$change.deleted
            if ($removed) {
                if ($relative -cnotin $deleted -or (Test-Path -LiteralPath (Join-Path $sourceRepo $relative))) {
                    throw "Deleted shader-reuse input is still present: $relative"
                }
            } elseif ((Hash-File (Join-Path $sourceRepo $relative)) -cne [string]$change.sha256) { throw "Changed-file SHA mismatch: $relative" }
        }
        $librarySources = @($tracked | Where-Object { $_ -match '^external/(DirectXTex|DirectXTK)/' })
        if (!$librarySources.Count) { throw 'DirectX library source inventory missing.' }
        foreach ($relative in $librarySources) {
            $cached = Join-Path $repo $relative
            if (!(Test-Path -LiteralPath $cached -PathType Leaf) -or
                (Hash-File $cached) -cne (Hash-File (Join-Path $sourceRepo $relative))) { throw "Cached library source differs: $relative" }
        }
        foreach ($name in 'DirectXTex','DirectXTK') {
            $entry = @($shaderReuse.libraries | Where-Object { $_.name -ceq $name })
            if ($entry.Count -ne 1) { throw "Missing library receipt: $name" }
            $folder = Join-Path $repo "out/x64/Release/$name"
            foreach ($pair in @(@('.lib','sha256'),@('.pdb','pdb_sha256'))) {
                $payload = Join-Path $folder ($name+$pair[0])
                if (!(Test-Path -LiteralPath $payload -PathType Leaf) -or
                    (Hash-File $payload) -cne [string]$entry[0].($pair[1])) { throw "Cached library payload changed: $payload" }
            }
        }
    }
    Sync-Tree $sourceRepo $repo $tracked
    Set-Location $repo
    # MSBuild tracks the persistent tree. Disabled archive-cache fingerprints
    # are not dependencies and must not scan the entire Windows SDK.
    $sourceCommit = (& git -C $sourceRepo -c "safe.directory=$sourceRepo" describe --always --abbrev=12 --dirty --exclude=*).Trim()
    if ($LASTEXITCODE -ne 0 -or !$sourceCommit) { throw 'Cannot identify source commit.' }
    $arguments = @("/p:ScriptCheckCommit=$sourceCommit",'EI_Rebuild.sln',"/m:$cores",'/nr:false','/t:Build','/p:Configuration=Release','/p:Platform=x64','/p:PreferredToolArchitecture=x64',"/p:VCToolsVersion=$vcversion","/p:WindowsTargetPlatformVersion=$sdkversion","/p:UserRootDir=$privateTemp\",'/nologo','/v:minimal')
    if ($shaderReuse) {
        $reuseFolder = Split-Path -Parent $ShaderResourceManifest
        $destination = Join-Path $repo 'out/x64/Release/RendererShaders'
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        $expectedNames = @('eid3d11_shaders.res','eid3d11_dxil.res','eid3d11_d3d12_shaders.res','eid3d11_camp_art.res')
        if (@($shaderReuse.files).Count -ne $expectedNames.Count) { throw 'Shader reuse payload count.' }
        foreach ($name in $expectedNames) {
            $entry = @($shaderReuse.files | Where-Object { $_.name -ceq $name })
            if ($entry.Count -ne 1 -or [string]$entry[0].sha256 -notmatch '^[0-9a-f]{64}$') { throw "Shader reuse entry: $name" }
            $source = Join-Path $reuseFolder $name
            if ((Hash-File $source) -cne [string]$entry[0].sha256) { throw "Shader resource SHA mismatch: $name" }
            $null = Copy-BuildFile $source (Join-Path $destination $name)
            if ((Hash-File (Join-Path $destination $name)) -cne [string]$entry[0].sha256) { throw "Shader resource copy mismatch: $name" }
        }
        $arguments += '/p:EICacheRendererShaders=true','/p:EICacheDirectXTex=true','/p:EICacheDirectXTK=true'
        Write-Host 'Verified accepted shader resources and existing DirectX libraries; shader generation is disabled.'
    }
    & (Join-Path $repo 'tools/prepare-worktree.ps1') -VSInstallDir $vs
    if (!$?) { throw 'Preparation failed.' }
    Write-Host "MSBuild workers: $cores; compiler /MP; isolated endpoint $env:_MSPDBSRV_ENDPOINT_"
    $arguments += "/flp:logfile=$task/build.log;verbosity=normal"
    Push-Location $repo
    try {
        & (Join-Path $PSScriptRoot 'invoke-build.ps1') $msbuild @arguments
        $buildExit = $LASTEXITCODE
    } finally { Pop-Location }
    if ($buildExit -ne 0) { throw "MSBuild failed ($buildExit); no serial retry." }
    if ($shaderReuse) {
        $log = Get-Content -LiteralPath (Join-Path $task 'build.log') -Raw
        if ($log -match '(?im)^\s*(?:\d+>\s*)?(?:"[^"\r\n]*[\\/])?(?:fxc|dxc|RendererShaderEmbed)\.exe(?:"|\s)' -or
            $log -match '(?im)shader_embed:|DXIL:\s*\d+ programs|CompileShaders\.cmd') {
            throw 'Shader compiler or shader generation appeared in build log.'
        }
        foreach ($entry in $shaderReuse.files) {
            $file = Join-Path $repo ('out/x64/Release/RendererShaders/' + [string]$entry.name)
            if ((Hash-File $file) -cne [string]$entry.sha256) { throw "Shader resource changed during build: $($entry.name)" }
        }
        $records = @($shaderReuse.resources)
        if (!$records.Count) { throw 'Compiled-resource comparison list is empty.' }
        Add-Type -Path (Join-Path $sourceRepo 'tools/VerifyShaderResources.cs')
        $names = [string[]]@($records | ForEach-Object { [string]$_.name })
        $languages = [int[]]@($records | ForEach-Object { [int]$_.language })
        $sizes = [long[]]@($records | ForEach-Object { [long]$_.size })
        $hashes = [string[]]@($records | ForEach-Object { [string]$_.sha256 })
        foreach ($image in @('out/x64/Release/EI_Rebuild/EIRebuild64.exe',
                              'out/x64/Release/EIRebuild64NavBake/EIRebuild64NavBake.exe')) {
            $count = [EIShaderResourceVerifier]::Verify((Join-Path $repo $image),$names,$languages,$sizes,$hashes)
            if ($count -ne $records.Count) { throw "Compiled-resource comparison incomplete: $image" }
            Write-Host "Verified $count accepted resources in $image."
        }
        Write-Host 'Shader reuse confirmed: no shader compilation or resource regeneration in build log.'
    }
    if ($PhysicsFxCheck) {
        # Keep the shared work tree locked through this dependent native check build.
        $checkArgs = @($arguments | Where-Object { $_ -notlike '*.sln' -and $_ -notlike '/flp:*' })
        $checkArgs = @($checkArgs | Where-Object { $_ -notlike '/t:*' })
        & (Join-Path $PSScriptRoot 'invoke-build.ps1') $msbuild (Join-Path $repo 'PhysicsFxRenderCheck.vcxproj') @checkArgs /p:BuildProjectReferences=false *> (Join-Path $task 'physics-render-build.txt')
        if ($LASTEXITCODE -ne 0) { throw 'Physics render check build failed.' }
    }
    foreach ($name in 'DirectXTex','DirectXTK') {
        $compilerPdb = Join-Path $repo "out/obj/x64/Release/$name/$name.compiler.pdb"
        if (Test-Path -LiteralPath $compilerPdb) { Copy-Item -LiteralPath $compilerPdb -Destination (Join-Path $repo "out/x64/Release/$name/") -Force }
    }
    $bytes = Trim-Cache
    $kit = Join-Path $task 'kit'
    if (Test-Path -LiteralPath $kit) { throw "Kit already exists: $kit; retain evidence and use a fresh task for timing." }
    New-Item -ItemType Directory -Path $kit | Out-Null
    foreach ($target in @(@('EI_Rebuild','EIRebuild64'),@('EIRebuild64NavBake','EIRebuild64NavBake'),@('EIRebuild64Pack','EIRebuild64Pack'),@('EIRebuild64ModelCheck','EIRebuild64ModelCheck'))) {
        Copy-Item -LiteralPath (Join-Path $repo "out/x64/Release/$($target[0])/$($target[1]).exe") -Destination $kit
        Copy-Item -LiteralPath (Join-Path $repo "out/x64/Release/$($target[0])/$($target[0]).pdb") -Destination (Join-Path $kit ($target[1] + '.pdb'))
    }
    $ffmpegSource = Join-Path $kit 'sources/ffmpeg/9.0.2'
    $null = New-Item -ItemType Directory -Path $ffmpegSource
    $archive = Join-Path $project 'deps/ffmpeg/9.0.2/downloads/ffmpeg-9.0.2.tar.xz'
    $sourceLock = Get-Content (Join-Path $repo 'third_party/ffmpeg/source.lock') -Raw | ConvertFrom-StringData
    if ((Hash-File $archive) -cne $sourceLock.sha256) { throw 'Unexpected FFmpeg source archive.' }
    Copy-Item -LiteralPath $archive -Destination $ffmpegSource
    foreach ($directory in 'third_party/ffmpeg','tools/ffmpeg') {
        $target = Join-Path $ffmpegSource $directory
        $null = New-Item -ItemType Directory -Path $target -Force
        Get-ChildItem -LiteralPath (Join-Path $repo $directory) -File |
            Where-Object Name -ne '.gitattributes' | Copy-Item -Destination $target
    }
    foreach ($name in 'avcodec-63.dll','avformat-63.dll','avutil-61.dll','swscale-10.dll','swresample-7.dll') { Copy-Item -LiteralPath (Join-Path $repo ('dependencies/ffmpeg/9.0.2/x64/bin/' + $name)) -Destination $kit }
    Copy-Item -LiteralPath (Join-Path $repo 'dependencies/physx/5.13.0/bin/PhysXGpu_64.dll') -Destination $kit
    Copy-Item -LiteralPath (Join-Path $repo 'dependencies/physx/5.13.0/bin/PhysXGpu_64.pdb') -Destination $kit
    foreach ($name in 'nvngx_dlss.dll','nvngx_dlssg.dll','nvngx_dlssd.dll') { Copy-Item -LiteralPath (Join-Path $repo ('dependencies/dlss/v310.9.16/lib/Windows_x86_64/rel/' + $name)) -Destination $kit }
    foreach ($name in 'amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_framegeneration_dx12.dll','amd_fidelityfx_upscaler_dx12.dll') { Copy-Item -LiteralPath (Join-Path $repo ('dependencies/fidelityfx/2.4.0/signedbin/' + $name)) -Destination $kit }
    New-Item -ItemType Directory -Path (Join-Path $kit 'D3D12') | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo 'dependencies/agility/1.620.2/build/native/bin/x64/D3D12Core.dll') -Destination (Join-Path $kit 'D3D12')
    Copy-Item -LiteralPath (Join-Path $repo 'out/x64/Release/EI_Rebuild/licenses') -Destination (Join-Path $kit 'licenses') -Recurse
    Copy-Item -LiteralPath $RendererMaterialsSource -Destination (Join-Path $kit 'eid3d_materials') -Recurse
    Copy-Item -LiteralPath (Join-Path $RendererAssetsSource 'eid3d_hosted') -Destination (Join-Path $kit 'eid3d_hosted') -Recurse
    Copy-Item -LiteralPath (Join-Path $repo 'external/owned-renderer/Host/d3d12/eid3d11_d3d12_pipelines.shipped.list') -Destination $kit
    Copy-Item -LiteralPath (Join-Path $repo 'external/owned-renderer/LICENSE.md') -Destination (Join-Path $kit 'licenses/EID3D11-LICENSE.md')
    Copy-Item -LiteralPath (Join-Path $repo 'external/owned-renderer/NOTICE.md') -Destination (Join-Path $kit 'licenses/EID3D11-NOTICE.md')
    Copy-Item -LiteralPath (Join-Path $repo 'THIRD_PARTY.md') -Destination $kit
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
    if ($PhysicsFxCheck) {
        & (Join-Path $repo 'tools/check-physics-fx-render.ps1') -OutputDirectory (Join-Path $task 'checks/render') -Prepared -SkipBuild -KitDirectory $kit
    }
    $clock.Stop()
    Write-Utf8 (Join-Path $task 'build-result.txt') @("Seconds=$($clock.Elapsed.TotalSeconds)","MSBuildWorkers=$cores","CacheBytes=$bytes","CacheRoot=$CacheRoot")
    Write-Host "Built kit in $($clock.Elapsed.TotalSeconds) seconds; cache $bytes bytes."
} catch {
    $clock.Stop()
    $bytes = 0
    # Failure reporting does not rescan a moving/unlocked cache.
    Write-Utf8 (Join-Path $task 'build-failure.txt') @("Seconds=$($clock.Elapsed.TotalSeconds)","MSBuildWorkers=$cores","CacheBytes=$bytes",$_.Exception.Message)
    throw
} finally {
    if ($lock) { $lock.Dispose() }
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name,$saved[$name],'Process') }
    Pop-Location
}
