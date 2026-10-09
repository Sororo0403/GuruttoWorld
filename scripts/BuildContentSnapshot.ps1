function Get-BuildContentInventory {
    param([string]$Content)
    $entries=[System.Collections.Generic.List[object]]::new()
    foreach($folder in @('Assets','Shaders')) {
        $source=Join-Path $Content $folder
        foreach($file in Get-ChildItem -LiteralPath $source -File -Recurse | Sort-Object FullName) {
            if($file.Name -match '\.tmp($|\.)') {continue}
            $relative="$folder/"+$file.FullName.Substring($source.Length).TrimStart('\','/').Replace('\','/')
            if($relative.StartsWith('Assets/Scripts/Bin/')) {continue}
            $entries.Add([pscustomobject]@{path=$relative;source=$file.FullName;bytes=$file.Length;hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash})
        }
    }
    return $entries.ToArray()
}
function Copy-BuildContentSnapshot {
    param([string]$Content,[string]$Destination)
    $before=@(Get-BuildContentInventory -Content $Content)
    foreach($entry in $before) {
        $target=Join-Path $Destination $entry.path
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $entry.source -Destination $target
        if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.hash) {throw "Content changed while copying: $($entry.path)"}
    }
    $after=@(Get-BuildContentInventory -Content $Content)
    $beforeText=($before | ForEach-Object {"$($_.path)|$($_.bytes)|$($_.hash)"}) -join "`n"
    $afterText=($after | ForEach-Object {"$($_.path)|$($_.bytes)|$($_.hash)"}) -join "`n"
    if($beforeText -cne $afterText) {throw 'Content changed while taking build snapshot. Save and build again.'}
}
