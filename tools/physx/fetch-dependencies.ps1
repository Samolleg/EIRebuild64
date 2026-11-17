param([Parameter(Mandatory=$true)][string]$ProjectRoot)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath($ProjectRoot)
if(!(Test-Path -LiteralPath (Join-Path $project 'RULES.md'))){throw 'Project RULES.md missing'}
$sdk=Join-Path $project 'deps/physx/5.6.1'
$cuda=Join-Path $project 'deps/cuda/12.8.1'
New-Item -ItemType Directory -Force -Path $sdk,$cuda | Out-Null
function Fetch([string]$url,[string]$file,[string]$sha){
    if(!(Test-Path -LiteralPath $file)){
        & curl.exe -fL --connect-timeout 30 --retry 2 $url -o $file
        if($LASTEXITCODE -ne 0){throw "Download failed: $url"}
    }
    if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha){throw "SHA256 mismatch: $file"}
}
$commit='5ca9f472105a90d70d957c243cb0ef36fe251a9f'
$archive=Join-Path $sdk 'source.zip'
Fetch "https://codeload.github.com/NVIDIA-Omniverse/PhysX/zip/$commit" $archive 'b20b5c497ae51e6556519323a999447adf1c24a8b6265ecaee23ac40fb62be12'
if(!(Test-Path -LiteralPath "$sdk/source/PhysX-$commit/physx/include/PxPhysicsAPI.h")){
    Expand-Archive -LiteralPath $archive -DestinationPath "$sdk/source" -Force
}
$packages=@(
    @('cuda_nvcc','12.8.93','9fdc70b4271ed9aad4d64cd7076a7d96ec36512d074b9995fe638de669197391'),
    @('cuda_cudart','12.8.90','4a39058fd8519444a81cfc7ae055d136f48d1a31ffa41ae255b35b2edd61e13b'),
    @('cuda_cccl','12.8.90','bd8548fa1ae82f92910bebc3079e14bd58c5a92aa64596d46bd610a478cb39d7'),
    @('cuda_nvtx','12.8.90','38dbd5941537fcb359137f26fe31b4e3528c504cc2a55fa10a9baaf0ff006187')
)
foreach($p in $packages){
    $name=$p[0]+'-windows-x86_64-'+$p[1]+'-archive'
    $zip=Join-Path $cuda ($name+'.zip')
    Fetch ('https://developer.download.nvidia.com/compute/cuda/redist/'+$p[0]+'/windows-x86_64/'+$name+'.zip') $zip $p[2]
    $unpacked=Join-Path "$cuda/packages" $name
    if(!(Test-Path -LiteralPath $unpacked)){Expand-Archive -LiteralPath $zip -DestinationPath "$cuda/packages"}
    foreach($item in Get-ChildItem -LiteralPath $unpacked){
        if($item.Name -match '^(LICENSE|EULA)'){
            New-Item -ItemType Directory -Force -Path "$cuda/licenses/$($p[0])" | Out-Null
            Copy-Item -LiteralPath $item.FullName -Destination "$cuda/licenses/$($p[0])" -Force
        }else{Copy-Item -LiteralPath $item.FullName -Destination $cuda -Recurse -Force}
    }
}
Write-Host "Pinned PhysX source: $sdk/source/PhysX-$commit"
Write-Host "Local CUDA compiler: $cuda/bin/nvcc.exe (no driver or system installation)"
