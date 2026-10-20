param([Parameter(Mandatory = $true)][string]$Project)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ProjectContext.ps1')
$projectContext = Get-ProjectContext -Project $Project
Sync-GeneratedGameProject -Context $projectContext
Write-Output "Updated generated build files: $($projectContext.root)"
