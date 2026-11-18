#requires -Version 7.6.10
#requires -PSEdition Core
param([string]$ProjectRoot,[switch]$Force)
$ErrorActionPreference='Stop'
if(!$ProjectRoot){$ProjectRoot=$PSScriptRoot;while(!(Test-Path -LiteralPath (Join-Path $ProjectRoot 'RULES.md'))){$ProjectRoot=Split-Path $ProjectRoot;if(!$ProjectRoot){throw 'Project root missing'}}}
$project=[IO.Path]::GetFullPath($ProjectRoot)
$sdk=Join-Path $project 'deps/physx/5.13.0'
$cuda=Join-Path $project 'deps/cuda/13.5.0'
$commit='7d6f6ac8b83e1610bdf715f1f2f1d6b718aee586'
$pristine=Join-Path $sdk "source/PhysX-$commit"
$source=Join-Path $sdk 'build-source'
$build=Join-Path $sdk 'build-v143'
$install=Join-Path $sdk 'sdk'
if(!(Test-Path -LiteralPath "$pristine/physx/include/PxPhysicsAPI.h") -or !(Test-Path -LiteralPath "$cuda/bin/nvcc.exe")){throw 'Run tools/physx/fetch-dependencies.ps1 explicitly first; normal build is offline.'}
$recipe=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest=Join-Path $install 'SHA256SUMS'
if(!$Force -and (Test-Path -LiteralPath "$install/recipe.sha256") -and (Get-Content "$install/recipe.sha256" -Raw).Trim() -eq $recipe -and (Test-Path -LiteralPath $manifest)){
    foreach($line in Get-Content -LiteralPath $manifest){$hash,$relative=$line -split '  ',2;$path=Join-Path $install $relative;if(!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $hash){throw "PhysX SDK payload changed: $relative"}}
    Write-Host 'Pinned PhysX 5.13.0 GPU SDK ready (verified hashes).';return
}
New-Item -ItemType Directory -Force -Path $sdk,$build,$install | Out-Null
$lock=[IO.File]::Open((Join-Path $sdk '.build.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$names='INCLUDE','LIB','PATH','CUDA_PATH','_MSPDBSRV_ENDPOINT_','CL','_CL_','LINK','_LINK_'
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try {
    if(!(Test-Path -LiteralPath $source)){Copy-Item -LiteralPath $pristine -Destination $source -Recurse}
    # Apply exact edits only to the private build copy of the pinned source.
    $encoding=[Text.Encoding]::GetEncoding(28591)
    $edits=@(
        @('physx/source/compiler/cmake/windows/CMakeLists.txt','IF(PX_COPY_EXTERNAL_DLL OR PUBLIC_RELEASE)','IF((PX_COPY_EXTERNAL_DLL OR PUBLIC_RELEASE) AND PX_BUILDSNIPPETS)')
        @('physx/source/compiler/cmakegpu/CMakeLists.txt',' -t0 -D_CONSOLE',' -t2 -D_CONSOLE')
        @('physx/source/cudamanager/src/CudaContextManager.cpp','cuCtxCreate(&mCtx, (unsigned int)flags, mDevHandle)','cuCtxCreate(&mCtx, nullptr, (unsigned int)flags, mDevHandle) /* EIRebuild64: regular CUDA 13 context */')
    )
    foreach($edit in $edits){
        $path=Join-Path $source $edit[0];$text=$encoding.GetString([IO.File]::ReadAllBytes($path));
        if($text.Contains($edit[1])){[IO.File]::WriteAllBytes($path,$encoding.GetBytes($text.Replace($edit[1],$edit[2])))}elseif(!$text.Contains($edit[2])){throw "Unexpected PhysX recipe source: $path"}
    }
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $version=(Get-Content "$vs/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" -Raw).Trim()
    $vc="$vs/VC/Tools/MSVC/$version"
    $windowsSdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
    $sv=(Get-ChildItem "$windowsSdk/Include" -Directory | Where-Object {Test-Path "$($_.FullName)/um/Windows.h"} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
    $cmake="$project/deps/cmake/4.5.0/bin/cmake.exe"
    $ninja="$project/deps/ninja/1.13.2/ninja.exe"
    $env:INCLUDE="$vc/include;$windowsSdk/Include/$sv/ucrt;$windowsSdk/Include/$sv/um;$windowsSdk/Include/$sv/shared"
    $env:LIB="$vc/lib/x64;$windowsSdk/Lib/$sv/ucrt/x64;$windowsSdk/Lib/$sv/um/x64"
    $env:PATH="$vc/bin/Hostx64/x64;$windowsSdk/bin/$sv/x64;$cuda/bin;$env:PATH"
    $env:CUDA_PATH=$cuda;$env:_MSPDBSRV_ENDPOINT_='PhysX-'+[guid]::NewGuid().ToString('N')
    foreach($name in 'CL','_CL_','LINK','_LINK_'){[Environment]::SetEnvironmentVariable($name,$null,'Process')}
    $px=($source+'/physx').Replace('\','/')
    $cuda=$cuda.Replace('\','/');$sdk=$sdk.Replace('\','/');$install=$install.Replace('\','/');$ninja=$ninja.Replace('\','/')
    & $cmake -S "$px/compiler/public" -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=release "-DCMAKE_CUDA_COMPILER=$cuda/bin/nvcc.exe" "-DCUDAToolkit_ROOT=$cuda" "-DPHYSX_ROOT_DIR=$px" -DTARGET_BUILD_PLATFORM=windows -DPX_GENERATE_GPU_PROJECTS=ON -DPX_GENERATE_GPU_REDUCED_ARCHITECTURES=ON -DCMAKE_CUDA_ARCHITECTURES=75 -DPX_GENERATE_STATIC_LIBRARIES=ON -DPX_BUILDSNIPPETS=OFF -DPX_BUILDPVDRUNTIME=OFF -DNV_USE_STATIC_WINCRT=ON "-DPX_OUTPUT_LIB_DIR=$sdk/output" "-DPX_OUTPUT_BIN_DIR=$sdk/output" "-DCMAKE_INSTALL_PREFIX=$install" -DPX_CMAKE_SUPPRESS_REGENERATION=ON
    if($LASTEXITCODE -ne 0){throw 'PhysX CMake configure failed'}
    & (Join-Path $PSScriptRoot '../invoke-build.ps1') $cmake --build $build --config release --target PhysX PhysXCommon PhysXFoundation PhysXExtensions PhysXPvdSDK PhysXGpu --parallel 2
    if($LASTEXITCODE -ne 0){throw 'PhysX source build failed'}
    New-Item -ItemType Directory -Force -Path "$install/lib","$install/bin","$install/licenses" | Out-Null
    Copy-Item -LiteralPath "$px/include" -Destination $install -Recurse -Force
    foreach($name in 'PhysX_static.lib','PhysXCommon_static.lib','PhysXFoundation_static.lib','PhysXExtensions_static.lib','PhysXPvdSDK_static.lib'){
        $file=@(Get-ChildItem "$sdk/output" -Recurse -File -Filter $name | Where-Object {$_.Directory.Name -eq 'release'})
        if($file.Count -ne 1){throw "Expected one release PhysX library: $name"};Copy-Item -LiteralPath $file[0].FullName -Destination "$install/lib" -Force
    }
    foreach($name in 'PhysXGpu_64.dll','PhysXGpu_64.pdb'){
        # Package the matching release binary and linker symbols.
        $file=@(Get-ChildItem "$sdk/output" -Recurse -File -Filter $name | Where-Object {$_.Directory.Name -eq 'release'})
        if($file.Count -ne 1){throw "Expected one GPU runtime: $name"};Copy-Item -LiteralPath $file[0].FullName -Destination "$install/bin/$name" -Force
    }
    Copy-Item -LiteralPath "$pristine/physx/LICENSE.md" -Destination "$install/licenses/PhysX-LICENSE.md" -Force
    Copy-Item -LiteralPath "$pristine/physx/THIRD_PARTY_NOTICES.md" -Destination "$install/licenses/PhysX-NOTICES.md" -Force
    Copy-Item -LiteralPath "$PSScriptRoot/PhysX-CHANGES.md" -Destination "$install/licenses" -Force
    foreach ($component in Get-ChildItem "$cuda/licenses" -Directory) {
        $label=$component.Name -replace '^cuda_',''
        Copy-Item -LiteralPath (Join-Path $component.FullName 'LICENSE') -Destination "$install/licenses/CUDA-$label-LICENSE.txt" -Force
    }
    $utf8=New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText("$install/recipe.sha256",$recipe,$utf8)
    $rows=@(Get-ChildItem -LiteralPath $install -File -Recurse | Where-Object Name -ne 'SHA256SUMS' | Sort-Object FullName | ForEach-Object {(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()+'  '+$_.FullName.Substring($install.Length+1).Replace('\','/')})
    [IO.File]::WriteAllLines($manifest,[string[]]$rows,$utf8)
    Write-Host "Built PhysX GPU SDK: $install"
} finally {foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')};$lock.Dispose()}
