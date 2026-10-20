. (Join-Path $PSScriptRoot 'ProjectContext.ps1')

function Get-SharedContentRoot {
    param([string]$Content)
    $descriptorPath = Join-Path $Content 'WP1.project.json'
    if (!(Test-Path -LiteralPath $descriptorPath -PathType Leaf)) { return $null }
    $engine = Get-ProjectEngineRoot -Content $Content
    $shared = [IO.Path]::GetFullPath((Join-Path $engine 'Content'))
    if (!(Test-Path -LiteralPath (Join-Path $shared 'Assets') -PathType Container) -or
        !(Test-Path -LiteralPath (Join-Path $shared 'Shaders') -PathType Container)) {
        throw "Shared content is missing: $shared"
    }
    return $shared
}

function Assert-SharedAssetIds {
    param([string]$Shared, [string]$Content)
    if (!$Shared) { return }
    foreach ($folder in @('Assets/Fonts', 'Shaders')) {
        $source = Join-Path $Content $folder
        if (!(Test-Path -LiteralPath $source)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $source -File -Recurse -Filter '*.meta') {
            $relative = $folder + '/' + $file.FullName.Substring($source.Length).TrimStart('\', '/')
            $common = Join-Path $Shared $relative
            if (Test-Path -LiteralPath $common -PathType Leaf) {
                $localId = (Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json).id
                $sharedId = (Get-Content -LiteralPath $common -Raw -Encoding UTF8 | ConvertFrom-Json).id
                if ($localId -cne $sharedId) { throw "Shared override must preserve asset ID: $relative" }
            }
        }
    }
}
