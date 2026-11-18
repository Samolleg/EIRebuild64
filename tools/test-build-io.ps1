#requires -Version 7.6.10
#requires -PSEdition Core
param([Parameter(Mandatory=$true)][string]$FixtureRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'build-io.ps1')
if([IO.Directory]::Exists($FixtureRoot)){throw 'Use a fresh fixture root.'}
$null=[IO.Directory]::CreateDirectory($FixtureRoot)
$source=Join-Path $FixtureRoot 'source';$dest=Join-Path $FixtureRoot 'dest'
$null=[IO.Directory]::CreateDirectory($source)
$script:checks=0
function Verify($Value,$Message){$script:checks++;if(!$Value){throw $Message}}
function Reject([scriptblock]$Action){$failed=$false;try{& $Action|Out-Null}catch{$failed=$true};Verify $failed 'Expected rejection.'}
[IO.File]::WriteAllText((Join-Path $source 'a.txt'),'one')
Sync-Tree $source $dest @('a.txt')
$out=Join-Path $dest 'a.txt';$stamp=[IO.File]::GetLastWriteTimeUtc($out)
Sync-Tree $source $dest @('a.txt')
Verify ([IO.File]::GetLastWriteTimeUtc($out) -eq $stamp) 'No-op restamped output.'
[IO.File]::WriteAllText((Join-Path $source 'a.txt'),'two')
[IO.File]::SetLastWriteTimeUtc((Join-Path $source 'a.txt'),$stamp.AddDays(-1))
Sync-Tree $source $dest @('a.txt')
Verify ([IO.File]::ReadAllText($out) -ceq 'two') 'Same-size older change missed.'
Verify ([IO.File]::GetLastWriteTimeUtc($out) -gt $stamp) 'Changed source did not invalidate native output.'
[IO.File]::WriteAllText((Join-Path $dest 'unowned.txt'),'keep')
Sync-Tree $source $dest @()
Verify (![IO.File]::Exists($out)) 'Removed owned source retained.'
Verify ([IO.File]::Exists((Join-Path $dest 'unowned.txt'))) 'Unowned output removed.'
Sync-Tree $source $dest @('a.txt')
Verify ([IO.File]::ReadAllText($out) -ceq 'two') 'Cannot repopulate an empty source list.'
Reject {Resolve-BuildChild $dest '../outside'}
$junction=Join-Path $FixtureRoot 'junction'
$null=New-Item -ItemType Junction -Path $junction -Target $source
Reject {Resolve-BuildChild $FixtureRoot 'junction/a.txt'}
[IO.File]::WriteAllText((Join-Path $dest '.ei-sources'),"../unowned.txt`r`n")
Reject {Sync-Tree $source $dest @()}
Reject {Assert-BuildCacheRoot (Join-Path $FixtureRoot 'cache/work/nested') (Join-Path $FixtureRoot 'cache')}
Assert-BuildCacheRoot (Join-Path $FixtureRoot 'separate') (Join-Path $FixtureRoot 'cache')
$path=Join-Path $FixtureRoot '.build.lock';$held=Enter-BuildLock $path
try{Reject {Enter-BuildLock $path 1}}finally{$held.Dispose()}
$next=Enter-BuildLock $path 1;$next.Dispose();Verify $true 'Lock reacquired.'
# Import pruning without running the build entry point.
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'build-task.ps1'),[ref]$null,[ref]$null)
foreach($name in 'Trim-Cache','Remove-CacheEntry'){
    $def=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$false)
    . ([scriptblock]::Create($def.Extent.Text))
}
$CacheRoot=Join-Path $FixtureRoot 'prune';$CacheVersions=1;$CacheLimitGB=1
$first=Join-Path $CacheRoot ('ShaderStore/'+('a'*64));$second=Join-Path $CacheRoot ('ShaderStore/'+('b'*64))
$null=[IO.Directory]::CreateDirectory($first);$null=[IO.Directory]::CreateDirectory($second)
[IO.File]::WriteAllText((Join-Path $first 'file'),'abc');[IO.File]::WriteAllText((Join-Path $second 'file'),'xyz')
[IO.Directory]::SetLastWriteTimeUtc($first,[datetime]::UtcNow.AddDays(-1))
$bytes=Trim-Cache
Verify ($bytes -eq 3 -and ![IO.Directory]::Exists($first) -and [IO.Directory]::Exists($second)) 'Version prune accounting failed.'
$null=[IO.Directory]::CreateDirectory((Join-Path $CacheRoot 'work'))
$large=[IO.File]::Create((Join-Path $CacheRoot 'work/large'));$large.SetLength(1GB+1);$large.Dispose()
Reject {Trim-Cache}
Verify ([IO.File]::Exists((Join-Path $CacheRoot 'work/large'))) 'Prune deleted persistent work.'
[IO.File]::Delete((Join-Path $CacheRoot 'work/large'))
Write-Host "PASS: $script:checks build IO checks."
