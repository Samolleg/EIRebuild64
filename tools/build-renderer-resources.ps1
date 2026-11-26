#requires -Version 7.6.10
#requires -PSEdition Core
param(
    [Parameter(Mandatory=$true)][string]$Embedder,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$RendererRoot,
    [Parameter(Mandatory=$true)][string]$Rc,
    [Parameter(Mandatory=$true)][string]$NMake,
    [Parameter(Mandatory=$true)][string]$Makefile,
    [Parameter(Mandatory=$true)][string]$Dxc,
    [Parameter(Mandatory=$true)][string]$Rtxdi,
    [Parameter(Mandatory=$true)][string]$HostD3D12
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'shader-build.ps1')
$out=[IO.Path]::GetFullPath($OutputDirectory)
$renderer=[IO.Path]::GetFullPath($RendererRoot)
$compiler=Join-Path ([Environment]::GetFolderPath('System')) 'd3dcompiler_47.dll'
$null=Invoke-ShaderGeneration $Embedder $compiler $out
$null=Invoke-ShaderRc $Rc (Join-Path $renderer 'Source/camp_art.rc') (Join-Path $out 'eid3d11_camp_art.res') $renderer
& (Join-Path $PSScriptRoot 'build-dxil.ps1') -NMake $NMake -Makefile $Makefile -OutputDirectory $out -Dxc $Dxc -Rtxdi $Rtxdi -HostD3D12 $HostD3D12
$dxil=Join-Path $out 'dxil'
$null=Invoke-ShaderRc $Rc (Join-Path $dxil 'dxil.rc') (Join-Path $out 'eid3d11_dxil.res') $dxil
$null=Invoke-ShaderRc $Rc (Join-Path $dxil 'd3d12_shaders.rc') (Join-Path $out 'eid3d11_d3d12_shaders.res') $dxil
