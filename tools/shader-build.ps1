#requires -Version 7.6.10
#requires -PSEdition Core
# Content-based build receipts: output timestamps survive unchanged work.
$script:ShaderBuildHelperPath=$MyInvocation.MyCommand.Path
function Get-ShaderFileHash([string]$Path) {
    $stream=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}
    finally{$stream.Dispose();$sha.Dispose()}
}
function Get-ShaderInputHash([string[]]$Files) {
    $lines=@(foreach($path in $Files){[IO.Path]::GetFullPath($path)+'='+(Get-ShaderFileHash $path)})
    $sha=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($lines -join "`n")))).Replace('-','').ToLowerInvariant()}
    finally{$sha.Dispose()}
}
function Write-ShaderText([string]$Path,[string]$Text) {
    if([IO.File]::Exists($Path) -and [IO.File]::ReadAllText($Path) -ceq $Text){return}
    $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Path)))
    $temp=$Path+'.tmp-'+[guid]::NewGuid().ToString('N')
    try{[IO.File]::WriteAllText($temp,$Text,[Text.UTF8Encoding]::new($false));Move-Item -LiteralPath $temp -Destination $Path -Force}
    finally{if([IO.File]::Exists($temp)){[IO.File]::Delete($temp)}}
}
function Test-ShaderReceipt([string]$Path,[string]$InputHash,[string[]]$Outputs) {
    if(![IO.File]::Exists($Path)){return $false}
    try{
        $receipt=[IO.File]::ReadAllText($Path)|ConvertFrom-Json
        if($receipt.schema -ne 1 -or $receipt.inputs -cne $InputHash -or @($receipt.outputs).Count -ne $Outputs.Count){return $false}
        for($i=0;$i -lt $Outputs.Count;$i++){
            $file=[IO.Path]::GetFullPath($Outputs[$i])
            if(![IO.File]::Exists($file) -or $receipt.outputs[$i].path -cne $file -or
               $receipt.outputs[$i].sha256 -cne (Get-ShaderFileHash $file)){return $false}
        }
        return $true
    }catch{return $false}
}
function Save-ShaderReceipt([string]$Path,[string]$InputHash,[string[]]$Outputs) {
    $values=@(foreach($file in $Outputs){[ordered]@{path=[IO.Path]::GetFullPath($file);sha256=(Get-ShaderFileHash $file)}})
    Write-ShaderText $Path ([ordered]@{schema=1;inputs=$InputHash;outputs=$values}|ConvertTo-Json -Depth 5)
}
function Get-ShaderRcInputs([string]$ResourceFile,[string]$WorkingDirectory) {
    $result=[Collections.Generic.List[string]]::new();$result.Add([IO.Path]::GetFullPath($ResourceFile))
    foreach($line in [IO.File]::ReadAllLines($ResourceFile)){
        if(!$line.Trim() -or $line.TrimStart().StartsWith('//')){continue}
        if($line -notmatch '^\s*\w+\s+RCDATA\s+"([^"]+)"\s*(?://.*)?$'){throw "Unsupported resource dependency: $line"}
        $name=$Matches[1].Replace('\\','\')
        $file=[IO.Path]::GetFullPath([IO.Path]::Combine($WorkingDirectory,$name))
        if(![IO.File]::Exists($file)){throw "Missing resource payload: $file"}
        $result.Add($file)
    }
    if($result.Count -lt 2){throw 'Empty resource table.'}
    return $result.ToArray()
}
function Invoke-ShaderRc([string]$Rc,[string]$ResourceFile,[string]$Output,[string]$WorkingDirectory,[scriptblock]$Run) {
    $inputs=@($Rc,$script:ShaderBuildHelperPath)+@(Get-ShaderRcInputs $ResourceFile $WorkingDirectory)
    $inputHash=Get-ShaderInputHash $inputs
    $receipt=$Output+'.inputs.json'
    if(Test-ShaderReceipt $receipt $inputHash @($Output)){Write-Host ('RC unchanged: '+[IO.Path]::GetFileName($Output));return $false}
    $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Output))
    $temp=$Output+'.tmp-'+[guid]::NewGuid().ToString('N')
    try{
        if($Run){& $Run $Rc $ResourceFile $temp $WorkingDirectory}else{
            Push-Location $WorkingDirectory
            try{& $Rc /nologo /fo $temp $ResourceFile;if($LASTEXITCODE -ne 0){throw "Resource compiler failed: $ResourceFile"}}
            finally{Pop-Location}
        }
        if(![IO.File]::Exists($temp) -or ([IO.FileInfo]::new($temp)).Length -eq 0){throw 'Resource compiler produced no output.'}
        if(![IO.File]::Exists($Output) -or (Get-ShaderFileHash $temp) -cne (Get-ShaderFileHash $Output)){
            Move-Item -LiteralPath $temp -Destination $Output -Force
        }
        Save-ShaderReceipt $receipt $inputHash @($Output)
        Write-Host ('RC verified: '+[IO.Path]::GetFileName($Output));return $true
    }finally{if([IO.File]::Exists($temp)){[IO.File]::Delete($temp)}}
}
function Get-ShaderGeneratedFiles([string]$Directory) {
    $files=@('eid3d11_shaders.bin','eid3d11_shaders.rc','dxil/dxil.rc','dxil/dxil.mak')|ForEach-Object{Join-Path $Directory $_}
    $sources=Join-Path $Directory 'dxil/src'
    if([IO.Directory]::Exists($sources)){$files+=@([IO.Directory]::EnumerateFiles($sources,'*.hlsl')|Sort-Object)}
    return $files
}
function Invoke-ShaderGeneration([string]$Embedder,[string]$Compiler,[string]$Directory,[scriptblock]$Run) {
    $receipt=Join-Path $Directory 'generation.inputs.json'
    $inputHash=Get-ShaderInputHash @($Embedder,$Compiler,$script:ShaderBuildHelperPath)
    if(Test-ShaderReceipt $receipt $inputHash @(Get-ShaderGeneratedFiles $Directory)){
        Write-Host 'Shader producer unchanged; generation skipped.';return $false
    }
    $null=[IO.Directory]::CreateDirectory($Directory)
    if($Run){& $Run $Embedder $Directory}else{
        Push-Location $Directory
        try{& $Embedder eid3d11_shaders.bin --dxil-sources dxil;if($LASTEXITCODE -ne 0){throw 'Shader generation failed.'}}
        finally{Pop-Location}
    }
    $outputs=@(Get-ShaderGeneratedFiles $Directory)
    if($outputs.Count -le 4){throw 'Shader generator produced no HLSL sources.'}
    foreach($file in $outputs){if(![IO.File]::Exists($file) -or ([IO.FileInfo]::new($file)).Length -eq 0){throw "Missing shader generation output: $file"}}
    Save-ShaderReceipt $receipt $inputHash $outputs
    return $true
}
