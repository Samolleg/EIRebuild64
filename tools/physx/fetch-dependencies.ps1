#requires -Version 7.6.10
#requires -PSEdition Core
param([Parameter(Mandatory=$true)][string]$ProjectRoot)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath($ProjectRoot)
if (!(Test-Path (Join-Path $project 'RULES.md'))) { throw 'Project RULES.md missing' }
$deps=Join-Path $project 'deps'
$cache=Join-Path $deps 'sdk-downloads'
$null=[IO.Directory]::CreateDirectory($cache)
$lock=Get-Content (Join-Path $PSScriptRoot 'sdk-lock.json') -Raw | ConvertFrom-Json

function Get-Archive($package) {
    $file=Join-Path $cache $package.archive
    if (!(Test-Path -LiteralPath $file)) {
        $temp=$file+'.'+[guid]::NewGuid().ToString('N')+'.part'
        try {
            & curl.exe --fail --location --silent --show-error --retry 2 --connect-timeout 30 $package.url -o $temp
            if ($LASTEXITCODE) { throw "Cannot fetch $($package.url)" }
            if ((Get-FileHash $temp).Hash -ne $package.sha256) { throw "SHA256 mismatch: $temp" }
            if (!(Test-Path -LiteralPath $file)) { [IO.File]::Move($temp,$file) }
        } finally { if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) } }
    }
    if ((Get-FileHash $file).Hash -ne $package.sha256) { throw "SHA256 mismatch: $file" }
    return $file
}
function Expand-Checked([string]$zip,[string]$stage,[int]$strip,[string[]]$members) {
    & tar.exe -xf $zip -C $stage --strip-components $strip @members
    if ($LASTEXITCODE) { throw "Cannot extract $zip" }
}
function Install-Directory([string]$relative,[scriptblock]$populate) {
    $target=[IO.Path]::GetFullPath((Join-Path $deps $relative))
    if ([IO.Directory]::Exists($target)) { return }
    $parent=[IO.Path]::GetDirectoryName($target)
    $stage=Join-Path $parent ('.extract-'+[guid]::NewGuid().ToString('N'))
    $null=[IO.Directory]::CreateDirectory($stage)
    try {
        & $populate $stage
        [IO.Directory]::Move($stage,$target)
    } finally {
        if ([IO.Directory]::Exists($stage) -and [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($stage)) -eq $parent) {
            Remove-Item -LiteralPath $stage -Recurse -Force
        }
    }
}

Install-Directory "cmake/$($lock.cmake.version)" { param($stage)
    Expand-Checked (Get-Archive $lock.cmake) $stage 1 @()
}
Install-Directory "ninja/$($lock.ninja.version)" { param($stage)
    Expand-Checked (Get-Archive $lock.ninja) $stage 0 @()
}
$source="PhysX-$($lock.physx.commit)"
Install-Directory "physx/$($lock.physx.version)/source/$source" { param($stage)
    Expand-Checked (Get-Archive $lock.physx) $stage 1 @("$source/physx","$source/LICENSE.md")
}
Install-Directory "fidelityfx-sdk/$($lock.fidelityfx.version)" { param($stage)
    $prefix="FidelityFX-SDK-$($lock.fidelityfx.commit)/Kits/FidelityFX"
    $paths='api/include','upscalers/include','framegeneration/include','docs/license.md','signedbin/amd_fidelityfx_loader_dx12.lib','signedbin/amd_fidelityfx_loader_dx12.dll','signedbin/amd_fidelityfx_framegeneration_dx12.dll','signedbin/amd_fidelityfx_upscaler_dx12.dll'
    Expand-Checked (Get-Archive $lock.fidelityfx) $stage 3 @($paths | ForEach-Object {"$prefix/$_"})
    [IO.File]::Move("$stage/docs/license.md","$stage/license.md")
    foreach ($dll in Get-ChildItem "$stage/signedbin/*.dll") {
        if ((Get-AuthenticodeSignature $dll.FullName).Status -ne 'Valid') { throw "Invalid AMD signature: $($dll.Name)" }
    }
}

Install-Directory "cuda/$($lock.cuda.version)" { param($stage)
    foreach ($package in $lock.cuda.packages) {
        $zip=Get-Archive $package
        $temp=Join-Path $stage '.package'
        $null=[IO.Directory]::CreateDirectory($temp)
        Expand-Checked $zip $temp 1 @()
        foreach ($entry in Get-ChildItem -LiteralPath $temp) {
            if ($entry.Name -match '^(LICENSE|EULA)') {
                $license=Join-Path $stage "licenses/$($package.name)"
                $null=[IO.Directory]::CreateDirectory($license)
                Copy-Item -LiteralPath $entry.FullName -Destination $license -Force
            } else {
                Copy-Item -LiteralPath $entry.FullName -Destination $stage -Recurse -Force
            }
        }
        if ([IO.Path]::GetFullPath($temp) -ne [IO.Path]::GetFullPath([IO.Path]::Combine($stage,'.package'))) { throw 'Unexpected package root' }
        Remove-Item -LiteralPath $temp -Recurse -Force
    }
}
Write-Output 'Project SDK archives verified on extraction. Normal engine builds are offline.'
