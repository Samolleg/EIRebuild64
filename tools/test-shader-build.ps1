#requires -Version 7.6.10
#requires -PSEdition Core
param([Parameter(Mandatory=$true)][string]$FixtureRoot,[string]$Rc)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'shader-build.ps1')
$root=[IO.Path]::GetFullPath($FixtureRoot)
if(Test-Path -LiteralPath $root){throw 'Use a new fixture root.'}
$null=[IO.Directory]::CreateDirectory($root)
$script:checks=0;$script:rcCalls=0;$script:generatorCalls=0
function Check([bool]$ok,[string]$why){$script:checks++;if(!$ok){throw $why}}
function Reject([scriptblock]$action,[string]$why){$bad=$false;try{& $action|Out-Null}catch{$bad=$true};Check $bad $why}
$compiler=Join-Path $root 'compiler.exe';Write-ShaderText $compiler 'compiler-one'
$payload=Join-Path $root 'payload.bin';Write-ShaderText $payload 'payload-one'
$resource=Join-Path $root 'input.rc';Write-ShaderText $resource 'TEST RCDATA "payload.bin"'
$output=Join-Path $root 'output.res'
$run={param($tool,$source,$dest,$directory)
    $script:rcCalls++
    # The test compiler deliberately produces the same bytes after a compiler
    # identity change, to exercise content-preserving publication.
    Write-ShaderText $dest (Get-ShaderInputHash @(Get-ShaderRcInputs $source $directory))
}
$null=Invoke-ShaderRc $compiler $resource $output $root $run
$stamp=[IO.File]::GetLastWriteTimeUtc($output)
Check (!(Invoke-ShaderRc $compiler $resource $output $root $run)) 'no-op RC executed'
Check ($script:rcCalls -eq 1 -and [IO.File]::GetLastWriteTimeUtc($output) -eq $stamp) 'no-op touched RC output'
$inputStamp=[IO.File]::GetLastWriteTimeUtc($payload)
Write-ShaderText $payload 'payload-two';[IO.File]::SetLastWriteTimeUtc($payload,$inputStamp)
$null=Invoke-ShaderRc $compiler $resource $output $root $run
Check ($script:rcCalls -eq 2) 'same-time input mutation missed'
$stamp=[IO.File]::GetLastWriteTimeUtc($output)
Write-ShaderText $compiler 'compiler-two'
$null=Invoke-ShaderRc $compiler $resource $output $root $run
Check ($script:rcCalls -eq 3 -and [IO.File]::GetLastWriteTimeUtc($output) -eq $stamp) 'identical compiler output unnecessarily restamped'
Write-ShaderText $output 'corrupted'
$null=Invoke-ShaderRc $compiler $resource $output $root $run
Check ($script:rcCalls -eq 4) 'corrupted output reused'
[IO.File]::Delete($output)
$null=Invoke-ShaderRc $compiler $resource $output $root $run
Check ($script:rcCalls -eq 5 -and [IO.File]::Exists($output)) 'missing output not rebuilt'
Write-ShaderText $resource '#include "untracked.h"'
Reject {Invoke-ShaderRc $compiler $resource $output $root $run} 'untracked include accepted'
Write-ShaderText $resource 'TEST RCDATA "missing.bin"'
Reject {Invoke-ShaderRc $compiler $resource $output $root $run} 'missing resource input accepted'
Write-ShaderText $resource 'TEST RCDATA "payload.bin"'

$embedder=Join-Path $root 'embedder.exe';Write-ShaderText $embedder 'embedder-one'
$directory=Join-Path $root 'generated'
$generate={param($exe,$dir)
    $script:generatorCalls++
    foreach($name in @('dxil/dxil.rc','dxil/dxil.mak','dxil/src/one.hlsl')){
        Write-ShaderText (Join-Path $dir $name) ('stable '+$name)
    }
}
$null=Invoke-ShaderGeneration $embedder $directory $generate
$binary=Join-Path $directory 'dxil/dxil.rc';$stamp=[IO.File]::GetLastWriteTimeUtc($binary)
Check (!(Invoke-ShaderGeneration $embedder $directory $generate)) 'no-op generation executed'
Write-ShaderText $embedder 'embedder-two'
$null=Invoke-ShaderGeneration $embedder $directory $generate
Check ($script:generatorCalls -eq 2 -and [IO.File]::GetLastWriteTimeUtc($binary) -eq $stamp) 'unchanged table was touched after producer change'
Check (!(Invoke-ShaderGeneration $embedder $directory $generate)) 'unchanged old binary kept generation permanently dirty'
[IO.File]::Delete((Join-Path $directory 'dxil/src/one.hlsl'))
$null=Invoke-ShaderGeneration $embedder $directory $generate
Check ($script:generatorCalls -eq 3) 'missing generated HLSL not repaired'
if($Rc){
    $actual=Join-Path $root 'native.res'
    $null=Invoke-ShaderRc $Rc $resource $actual $root
    $stamp=[IO.File]::GetLastWriteTimeUtc($actual);$hash=Get-ShaderFileHash $actual
    Check (!(Invoke-ShaderRc $Rc $resource $actual $root)) 'real RC invoked on no-op'
    Check ([IO.File]::GetLastWriteTimeUtc($actual) -eq $stamp -and (Get-ShaderFileHash $actual) -ceq $hash) 'real resource changed on no-op'
}
Write-Host "PASS $script:checks shader build checks; no DXC or shader compiler invoked"
