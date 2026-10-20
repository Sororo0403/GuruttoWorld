param(
    [Parameter(Mandatory=$true)][string]$Destination,
    [ValidatePattern('^[A-Za-z][A-Za-z0-9_]{0,63}$')][string]$Name='MyGame',
    [string]$TemplateContent
)
$ErrorActionPreference='Stop'
$engineRoot=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
. (Join-Path $PSScriptRoot 'ProjectContext.ps1')
$destinationRoot=[IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $destinationRoot) { throw 'Choose a new folder; existing projects are never overwritten.' }
if (!$TemplateContent) { $TemplateContent=Join-Path $engineRoot 'Content/Templates/MinimalGame' }
$TemplateContent=[IO.Path]::GetFullPath($TemplateContent)
foreach ($required in @('Assets/Project.json')) {
    if (!(Test-Path -LiteralPath (Join-Path $TemplateContent $required))) { throw "Template is missing $required" }
}
function Write-ProjectFile([string]$Path,[string]$Text) {
    [IO.File]::WriteAllText($Path,($Text.Replace("`r`n","`n").Replace("`n","`r`n")+"`r`n"),[Text.UTF8Encoding]::new($false))
}
New-Item -ItemType Directory -Path $destinationRoot | Out-Null
. (Join-Path $PSScriptRoot 'BuildContentSnapshot.ps1')
Copy-BuildContentSnapshot -Content $TemplateContent -Destination $destinationRoot -IncludeShared $false
# 複製元の設定保存先と旧設定の移行指定を、新しいゲームへ引き継がない。
$settingsPath = Join-Path $destinationRoot 'Assets/Project.json'
$settings = Get-Content -LiteralPath $settingsPath -Raw -Encoding UTF8 | ConvertFrom-Json
$settings | Add-Member -NotePropertyName importLegacySettings -NotePropertyValue $false -Force
Write-ProjectFile $settingsPath ($settings | ConvertTo-Json -Depth 100)
$metadataPath = "$settingsPath.meta"
$metadata = if (Test-Path -LiteralPath $metadataPath) {
    Get-Content -LiteralPath $metadataPath -Raw -Encoding UTF8 | ConvertFrom-Json
} else {
    [pscustomobject]@{ version = 1; id = ''; scale = 1; flipV = $false; previousPaths = @() }
}
$metadata.id = [Guid]::NewGuid().ToString('N')
Write-ProjectFile $metadataPath ($metadata | ConvertTo-Json -Depth 100)
$descriptor=[ordered]@{version=1;engineContract=1;name=$Name;projectId=$metadata.id;engineRoot=$engineRoot;project="$Name.vcxproj";solution="$Name.slnx";generatedProjects=$true}
Write-ProjectFile (Join-Path $destinationRoot 'WP1.project.json') ($descriptor | ConvertTo-Json)
Sync-GeneratedGameProject -Context (Get-ProjectContext -Project $destinationRoot -RequireEngineRoot $engineRoot)
Write-ProjectFile (Join-Path $destinationRoot '.gitignore') "generated/`nAssets/Scripts/Bin/`n*.vcxproj.user`n.vs/"
Write-Output "Created game project: $destinationRoot"
Write-Output "Build $Name.slnx (Debug / Development / Release, x64)."
