param(
    [Parameter(Mandatory=$true)][string]$NMake,
    [Parameter(Mandatory=$true)][string]$Makefile,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$Dxc,
    [Parameter(Mandatory=$true)][string]$Rtxdi,
    [Parameter(Mandatory=$true)][string]$HostD3D12
)
$ErrorActionPreference='Stop'
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$dxil=Join-Path $OutputDirectory 'dxil'
$resourceList=Join-Path $dxil 'dxil.rc'
if (!(Test-Path -LiteralPath $resourceList)) { throw 'Generate DXIL sources before compiling them.' }
$expected=@(foreach ($line in [IO.File]::ReadAllLines($resourceList)) {
    if ($line -match '^\w+ RCDATA "([A-Za-z0-9_.-]+\.dxil)"$') { Join-Path $dxil $Matches[1] }
})
if (!$expected.Count) { throw 'Empty DXIL resource table.' }
# A rejected DXC program is an empty file in the author recipe. Retry that
# exact output and refuse the kit if any expected program remains missing.
foreach ($path in $expected) {
    if ([IO.File]::Exists($path) -and (Get-Item -LiteralPath $path).Length -eq 0) {
        if (![IO.Path]::GetFullPath($path).StartsWith($dxil+'\','OrdinalIgnoreCase')) { throw 'Unsafe DXIL output.' }
        Remove-Item -LiteralPath $path
    }
}
# Source timestamps alone cannot notice a changed compiler or included SDK.
# Preserve this stamp byte-for-byte on unchanged inputs so incremental builds
# reuse DXIL across task worktrees and acceptance builds.
$compilerDir=Split-Path -Parent $Dxc
$inputs=@($Dxc,(Join-Path $compilerDir 'dxcompiler.dll'),(Join-Path $compilerDir 'dxil.dll'))
$inputs+=@(Get-ChildItem -LiteralPath (Join-Path $Rtxdi 'Include') -File -Recurse | Sort-Object FullName | ForEach-Object FullName)
$fingerprint=(@($inputs | ForEach-Object { $_+'='+ (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }) -join "`n")
$stamp=Join-Path $dxil 'toolchain.stamp'
if (![IO.File]::Exists($stamp) -or [IO.File]::ReadAllText($stamp) -cne $fingerprint) {
    [IO.File]::WriteAllText($stamp,$fingerprint,[Text.UTF8Encoding]::new($false))
}
function Quote-Argument([string]$Value) {
    return '"' + ([regex]::Replace(([regex]::Replace($Value, '(\\*)"', '$1$1\"')), '(\\+)$', '$1$1')) + '"'
}
function Start-Part([string]$Target) {
    $arguments=@('/nologo','/f',$Makefile,$Target,"DXC=$Dxc","RTXDI_SDK=$Rtxdi","HOST_D3D12=$HostD3D12","TOOLCHAIN_STAMP=$stamp")
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$NMake; $info.Arguments=($arguments | ForEach-Object { Quote-Argument $_ }) -join ' '
    $info.WorkingDirectory=$OutputDirectory; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($info)
    [pscustomobject]@{ Process=$process; Output=$process.StandardOutput.ReadToEndAsync(); Error=$process.StandardError.ReadToEndAsync(); Target=$Target }
}
function Complete-Part($Job) {
    $Job.Process.WaitForExit()
    $output=$Job.Output.Result; $errorText=$Job.Error.Result
    $code=$Job.Process.ExitCode; $Job.Process.Dispose()
    if ($output) { Write-Output $output.TrimEnd() }
    if ($errorText) { Write-Output $errorText.TrimEnd() }
    if ($code -ne 0) { throw "DXIL $($Job.Target) failed ($code)." }
}
# The author emits sixteen independent parts. Bound concurrency by the CPU;
# every output has one producer and remains under the parent MSBuild job.
$workers=[Math]::Max(1,[Math]::Min(16,[Environment]::ProcessorCount))
Write-Output "DXIL: $($expected.Count) programs; $workers workers; incremental outputs retained."
for ($first=0; $first -lt 16; $first+=$workers) {
    $jobs=@(for ($part=$first; $part -lt [Math]::Min(16,$first+$workers); $part++) { Start-Part "dxil_part_$part" })
    foreach ($job in $jobs) { Complete-Part $job }
}
$failed=@($expected | Where-Object { ![IO.File]::Exists($_) -or (Get-Item -LiteralPath $_).Length -eq 0 })
if ($failed.Count) {
    foreach ($path in $failed | Select-Object -First 12) {
        Write-Output "DXIL rejected: $([IO.Path]::GetFileName($path))"
        $log=Join-Path $dxil ('log/'+[IO.Path]::GetFileNameWithoutExtension($path)+'.txt')
        if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 12 }
    }
    throw "DXC rejected $($failed.Count) of $($expected.Count) programs; no kit may be produced."
}
Complete-Part (Start-Part 'native')
Write-Output "DXIL: all $($expected.Count) expected programs are present and nonempty."
