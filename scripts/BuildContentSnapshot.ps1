. (Join-Path $PSScriptRoot 'FileHash.ps1')
. (Join-Path $PSScriptRoot 'ContentLayers.ps1')
function Get-BuildContentInventory {
    param([string]$Content, [bool]$IncludeShared = $true)
    $Content = [IO.Path]::GetFullPath($Content)
    if (!(Test-Path -LiteralPath (Join-Path $Content 'Assets') -PathType Container)) { throw 'Content Assets folder is missing.' }
    $entries = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    $roots = @()
    $shared = Get-SharedContentRoot -Content $Content
    Assert-SharedAssetIds -Shared $shared -Content $Content
    if ($IncludeShared -and $shared) { $roots += $shared }
    $roots += $Content
    foreach ($root in $roots) {
        foreach ($folder in @('Assets', 'Shaders')) {
            $source = Join-Path $root $folder
            if (!(Test-Path -LiteralPath $source -PathType Container)) { continue }
            foreach ($file in Get-ChildItem -LiteralPath $source -File -Recurse | Sort-Object FullName) {
                if ($file.Name -match '\.tmp($|\.)') { continue }
                $relative = "$folder/" + $file.FullName.Substring($source.Length).TrimStart('\', '/').Replace('\', '/')
                if ($relative.StartsWith('Assets/Scripts/Bin/') -or $relative.StartsWith('Shaders/Compiled/')) { continue }
                $entries[$relative] = [pscustomobject]@{
                    path = $relative
                    source = $file.FullName
                    bytes = $file.Length
                    hash = (Get-Wp1FileHash -LiteralPath $file.FullName)
                }
            }
        }
    }
    return @($entries.Values | Sort-Object path)
}
function Copy-BuildContentSnapshot {
    param([string]$Content,[string]$Destination, [bool]$IncludeShared = $true)
    $before=@(Get-BuildContentInventory -Content $Content -IncludeShared $IncludeShared)
    foreach($entry in $before) {
        $target=Join-Path $Destination $entry.path
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $entry.source -Destination $target
        if((Get-Wp1FileHash -LiteralPath $target) -ne $entry.hash) {throw "Content changed while copying: $($entry.path)"}
    }
    $after=@(Get-BuildContentInventory -Content $Content -IncludeShared $IncludeShared)
    $beforeText=($before | ForEach-Object {"$($_.path)|$($_.bytes)|$($_.hash)"}) -join "`n"
    $afterText=($after | ForEach-Object {"$($_.path)|$($_.bytes)|$($_.hash)"}) -join "`n"
    if($beforeText -cne $afterText) {throw 'Content changed while taking build snapshot. Save and build again.'}
}
