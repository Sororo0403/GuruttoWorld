param([Parameter(Mandatory = $true)][string]$WorkRoot)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$fixture = Join-Path ([IO.Path]::GetFullPath($WorkRoot)) ('project-settings-' + [Guid]::NewGuid().ToString('N'))
$template = Join-Path $fixture 'Template'
$sourceSettings = Join-Path $template 'Assets/Project.json'
New-Item -ItemType Directory -Path (Join-Path $template 'Assets/Scenes') -Force | Out-Null
$encoding = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($sourceSettings, '{"title":"Same title","startupScene":"Assets/Scenes/Main.json","width":1280,"height":720,"importLegacySettings":true}', $encoding)
[IO.File]::WriteAllText("$sourceSettings.meta", '{"version":1,"id":"11111111111111111111111111111111","scale":1,"flipV":false,"previousPaths":[]}', $encoding)
[IO.File]::WriteAllText((Join-Path $template 'Assets/Scenes/Main.json'), '{}', $encoding)
$before = (Get-FileHash -LiteralPath $sourceSettings).Hash
$beforeMetadata = (Get-FileHash -LiteralPath "$sourceSettings.meta").Hash
$ids = @()
. (Join-Path $repo 'scripts/BuildContentSnapshot.ps1')
foreach ($name in @('FirstGame', 'SecondGame')) {
    $project = Join-Path $fixture $name
    & (Join-Path $repo 'scripts/NewGameProject.ps1') -Destination $project -Name $name -TemplateContent $template
    $settings = Get-Content -LiteralPath (Join-Path $project 'Assets/Project.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $metadata = Get-Content -LiteralPath (Join-Path $project 'Assets/Project.json.meta') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($settings.title -ne 'Same title' -or $settings.importLegacySettings -ne $false) {
        throw 'A cloned game inherited the legacy-settings migration policy.'
    }
    if ($metadata.id -notmatch '^[a-f0-9]{32}$' -or $metadata.id -eq '11111111111111111111111111111111') {
        throw 'A cloned game reused the template settings ID.'
    }
    $ids += $metadata.id
    $package = Join-Path $fixture "$name-Package"
    Copy-BuildContentSnapshot -Content $project -Destination $package
    $packagedMetadata = Get-Content -LiteralPath (Join-Path $package 'Assets/Project.json.meta') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($packagedMetadata.id -ne $metadata.id) { throw 'Packaging changed the settings ID.' }
}
if ($ids[0] -eq $ids[1]) { throw 'Different games share a settings ID.' }
if ((Get-FileHash -LiteralPath $sourceSettings).Hash -ne $before -or
    (Get-FileHash -LiteralPath "$sourceSettings.meta").Hash -ne $beforeMetadata) {
    throw 'Creating games changed the template.'
}

# 古いテンプレートにプロジェクトのメタデータがなくても、新規ゲームには固定IDを付与する。
Remove-Item -LiteralPath "$sourceSettings.meta"
$project = Join-Path $fixture 'NoMetadataGame'
& (Join-Path $repo 'scripts/NewGameProject.ps1') -Destination $project -Name NoMetadataGame -TemplateContent $template
$metadata = Get-Content -LiteralPath (Join-Path $project 'Assets/Project.json.meta') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($metadata.id -notmatch '^[a-f0-9]{32}$') { throw 'The new game has no valid settings ID.' }
Write-Output 'PASS: distinct cloned-game IDs, explicit migration policy, stable package identity and unchanged templates.'
