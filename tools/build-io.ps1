# Local build IO. Compare bytes, preserve unchanged timestamps, reject links.
function Hash-File([string]$path) {
    $stream=[IO.File]::OpenRead($path);$sha=[Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
    finally {$stream.Dispose();$sha.Dispose()}
}
function Get-BuildFiles([string]$Root) {
    $directory=[IO.DirectoryInfo]::new($Root)
    if($directory.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Build directory is a link: $Root"}
    foreach($entry in $directory.EnumerateFileSystemInfos()) {
        if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Build input is a link: $($entry.FullName)"}
        if($entry -is [IO.DirectoryInfo]){Get-BuildFiles $entry.FullName}else{$entry}
    }
}
function Resolve-BuildChild([string]$Root,[string]$Relative,[Collections.Generic.HashSet[string]]$CheckedDirectories) {
    $rootPath=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $path=[IO.Path]::GetFullPath([IO.Path]::Combine($rootPath,$Relative))
    if(!$path.StartsWith($rootPath+'\',[StringComparison]::OrdinalIgnoreCase)){throw "Build path escapes root: $Relative"}
    $ancestor=$path;$leaf=$true
    while($ancestor){
        if(!$leaf -and $null -ne $CheckedDirectories -and $CheckedDirectories.Contains($ancestor)){break}
        $attributes=$null
        try{$attributes=[IO.File]::GetAttributes($ancestor)}catch [IO.FileNotFoundException]{}catch [IO.DirectoryNotFoundException]{}
        if($null -ne $attributes -and ($attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Build path is a link: $ancestor"}
        if(!$leaf -and $null -ne $CheckedDirectories -and [IO.Directory]::Exists($ancestor)){$null=$CheckedDirectories.Add($ancestor)}
        $ancestor=[IO.Path]::GetDirectoryName($ancestor);$leaf=$false
    }
    $path
}
function Copy-BuildFile([string]$Source,[string]$Destination) {
    if([IO.File]::Exists($Destination) -and ([IO.FileInfo]::new($Source)).Length -eq ([IO.FileInfo]::new($Destination)).Length -and
       (Hash-File $Source) -ceq (Hash-File $Destination)){return $false}
    $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination))
    [IO.File]::Copy($Source,$Destination,$true)
    [IO.File]::SetLastWriteTimeUtc($Destination,[datetime]::UtcNow)
    return $true
}
function Sync-Tree([string]$source,[string]$destination,[string[]]$files) {
    # Scope the directory validation cache to this sync, never across build phases.
    $checked=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $null=[IO.Directory]::CreateDirectory($destination)
    $listPath=Resolve-BuildChild $destination '.ei-sources'
    $previous=@();if([IO.File]::Exists($listPath)){$previous=[IO.File]::ReadAllLines($listPath)}
    $copied=0;$removed=0
    foreach($relative in $files){
        $from=Resolve-BuildChild $source $relative $checked;$to=Resolve-BuildChild $destination $relative $checked
        if(![IO.File]::Exists($from)){throw "Source disappeared: $from"}
        if(Copy-BuildFile $from $to){$copied++}
    }
    $current=[Collections.Generic.HashSet[string]]::new([string[]]$files,[StringComparer]::OrdinalIgnoreCase)
    foreach($relative in $previous){
        if(!$current.Contains($relative)){$gone=Resolve-BuildChild $destination $relative;if([IO.File]::Exists($gone)){[IO.File]::Delete($gone);$removed++}}
    }
    $text=if($files.Count){($files -join "`r`n")+"`r`n"}else{''}
    if(![IO.File]::Exists($listPath) -or [IO.File]::ReadAllText($listPath) -cne $text){[IO.File]::WriteAllText($listPath,$text,[Text.UTF8Encoding]::new($false))}
    Write-Host "Persistent build tree: $copied changed, $removed removed, $($files.Count) sources."
}
function Copy-BuildTree([string]$Source,[string]$Destination) {
    $checked=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($file in Get-BuildFiles $Source){
        $relative=$file.FullName.Substring($Source.TrimEnd('\','/').Length+1)
        $null=Copy-BuildFile $file.FullName (Resolve-BuildChild $Destination $relative $checked)
    }
}
function Assert-BuildCacheRoot([string]$Root,[string]$Canonical) {
    $rootPath=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $canonicalPath=[IO.Path]::GetFullPath($Canonical).TrimEnd('\')
    if($rootPath.StartsWith($canonicalPath+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'An independent cache cannot be nested in the shared build cache.'}
    $parent=[IO.Path]::GetDirectoryName($rootPath)
    while($parent){
        if([IO.File]::Exists((Join-Path $parent '.build.lock'))){throw "Cache nested in another cache: $parent"}
        $parent=[IO.Path]::GetDirectoryName($parent)
    }
}
function Enter-BuildLock([string]$Path,[int]$TimeoutSeconds=120) {
    $timer=[Diagnostics.Stopwatch]::StartNew();$notice=0
    while($true){
        try{return [IO.File]::Open($Path,'OpenOrCreate','ReadWrite','None')}
        catch [IO.IOException]{
            if(($_.Exception.HResult -band 0xffff) -notin 32,33){throw}
            if($timer.Elapsed.TotalSeconds -ge $TimeoutSeconds){throw "Timed out waiting for build cache: $Path"}
            if($timer.Elapsed.TotalSeconds -ge $notice){Write-Host "Waiting for another build to release $Path";$notice+=10}
            Start-Sleep -Milliseconds 200
        }
    }
}
