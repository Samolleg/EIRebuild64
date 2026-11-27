# Read the accepted component version independently of the game revision.
function Read-NavBakeRevision([string]$Path) {
    $value = [IO.File]::ReadAllText($Path).Trim()
    if ($value -cnotmatch '^(0|[1-9][0-9]*)$') { throw "Invalid NavBake revision in $Path." }
    return [uint32]$value
}

function Get-NavBakeReleasePlan([string]$CurrentKit, [uint32]$Requested) {
    if (![IO.Directory]::Exists($CurrentKit)) {
        if ($Requested -eq 0) { throw 'No accepted NavBake to retain. Bootstrap a separate NavBake release.' }
        return @{Build=$true}
    }
    $manifest = @{}
    foreach ($line in [IO.File]::ReadLines((Join-Path $CurrentKit 'SHA256SUMS'))) {
        if ($line -cmatch '^([a-f0-9]{64})  (NavBake\.release|EIRebuild64NavBake\.(?:exe|pdb))$') {
            $manifest.Add($Matches[2],$Matches[1])
        }
    }
    foreach ($name in 'EIRebuild64NavBake.exe','EIRebuild64NavBake.pdb') {
        if (!$manifest.ContainsKey($name)) { throw "Missing accepted manifest entry: $name" }
    }
    $revision = 0
    $marker = Join-Path $CurrentKit 'NavBake.release'
    if ($manifest.ContainsKey('NavBake.release') -or [IO.File]::Exists($marker)) {
        if ((Hash-File $marker) -cne $manifest['NavBake.release']) { throw 'Invalid accepted NavBake release marker.' }
        $revision = Read-NavBakeRevision $marker
    }
    if ($Requested -lt $revision) { throw "NavBake release $revision is already accepted. Update the checkout before building." }
    return @{Build=($Requested -gt $revision); Exe=$manifest['EIRebuild64NavBake.exe']; Pdb=$manifest['EIRebuild64NavBake.pdb']}
}
