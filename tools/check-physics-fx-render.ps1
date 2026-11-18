#requires -Version 7.6.10
#requires -PSEdition Core
param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$GameRoot='C:\GOG Games\PZ',[switch]$Prepared,[switch]$SkipBuild,[string]$KitDirectory)
$ErrorActionPreference='Stop'
$built=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$lock=[IO.File]::Open((Join-Path $out '.check.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$savedEndpoint=$env:_MSPDBSRV_ENDPOINT_;$env:_MSPDBSRV_ENDPOINT_='PhysXCheck-'+[guid]::NewGuid().ToString('N')
try {
    if(!$Prepared){
        & "$built/tools/prepare-worktree.ps1" -VSInstallDir $vs
        if(!$?){throw 'Check dependency preparation failed'}
    }
    if(!$SkipBuild){
        & (Join-Path $PSScriptRoot 'invoke-build.ps1') "$vs/MSBuild/Current/Bin/MSBuild.exe" "$built/PhysicsFxRenderCheck.vcxproj" /nologo /v:minimal /m:4 /nr:false /p:Configuration=Release /p:Platform=x64 *> "$out/build.txt"
        if($LASTEXITCODE -ne 0){Get-Content "$out/build.txt" -Tail 30;throw 'Native render check build failed'}
    }
    $builtBin=Join-Path $built 'out/x64/Release/PhysicsFxRenderCheck'
    $bin=Join-Path $out 'bin'
    New-Item -ItemType Directory -Force -Path $bin | Out-Null
    Copy-Item -LiteralPath "$builtBin/PhysicsFxRenderCheck.exe" -Destination $bin -Force
    $repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $kit=$KitDirectory;if(!$kit){$kit=Join-Path (Split-Path $repo) 'kit'}
    foreach($file in Get-ChildItem -LiteralPath $kit -Filter '*.dll' -File){Copy-Item -LiteralPath $file.FullName -Destination $bin -Force}
    foreach($name in 'eid3d_materials','eid3d_hosted'){
        if(!(Test-Path -LiteralPath (Join-Path $bin $name))){Copy-Item -LiteralPath (Join-Path $kit $name) -Destination $bin -Recurse}
    }
    foreach($name in 'nvngx_dlss.dll','nvngx_dlssg.dll','nvngx_dlssd.dll','amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_framegeneration_dx12.dll','amd_fidelityfx_upscaler_dx12.dll') {
        $source=Get-ChildItem "$built/dependencies" -Filter $name -Recurse -File | Select-Object -First 1
        if($source){Copy-Item -LiteralPath $source.FullName -Destination $bin -Force}
    }
    $agility="$built/dependencies/agility/1.620.2/build/native/bin/x64/D3D12Core.dll"
    New-Item -ItemType Directory -Force "$bin/D3D12" | Out-Null
    Copy-Item -LiteralPath $agility -Destination "$bin/D3D12" -Force
    # Test is a renderer harness, never the game executable; no game writes.
    Push-Location $bin
    try {
        foreach($api in 11,12){
            $destination=Join-Path $out "d3d$api"
            $ErrorActionPreference='Continue'
            & "$bin/PhysicsFxRenderCheck.exe" $GameRoot $destination $api *> "$out/d3d$api-result.txt"
            $nativeExit=$LASTEXITCODE
            $ErrorActionPreference='Stop'
            Get-Content "$out/d3d$api-result.txt"
            if($nativeExit -ne 0){throw "D3D$api native debris check failed ($nativeExit)"}
        }
    } finally {Pop-Location}
} finally {$lock.Dispose();$env:_MSPDBSRV_ENDPOINT_=$savedEndpoint}
