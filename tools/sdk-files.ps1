function Get-LocalSdkFiles([string]$Source, [string]$Pattern) {
    $path = Join-Path $Source $Pattern
    $leaf = Split-Path -Leaf $path
    if ([Management.Automation.WildcardPattern]::ContainsWildcardCharacters($leaf)) {
        # PS 5.1 combines a wildcard Path with -File before descending into
        # child directories. An Include tree with no root files becomes empty.
        # Keep the directory literal and apply the filename filter separately.
        $directory = Split-Path -Parent $path
        if (![IO.Directory]::Exists($directory)) { throw "Missing SDK directory: $directory" }
        Get-ChildItem -LiteralPath $directory -Filter $leaf -File -Recurse -ErrorAction Stop
    } else {
        # A pinned filename selects only that file, never same-named descendants.
        if (![IO.File]::Exists($path)) { throw "Missing SDK file: $path" }
        $file = Get-Item -LiteralPath $path -ErrorAction Stop
        if ($file.PSIsContainer) { throw "Expected SDK file: $path" }
        $file
    }
}
